# NativeAOT C160 — Stable Application Identity and Snapshot ABI v2

Status: **PASS — C160 proof, clean ABI-v2 production install, and ordinary boots completed.**

## C159 blocker and C160 scope

C159 established that bare-metal Task Manager reads `AppManager`, which is a bounded table of 16 application instances plus a separate shell surface. Its rows used compacted array positions as selection state and exposed no lifetime token. The hosted server process table, kernel processes, scheduler threads, AppManager instances, and the managed logical application surface are distinct sources.

C160 adds a stable lifetime token and one synchronous, read-only snapshot callback. It does not add a Managed Task Manager, task mutation call, process abstraction, event stream, polling service, or performance accounting.

## AppManager lifecycle audit

`AppManager::init` clears the registered metadata and running instance arrays, then `registerKernelApps` registers the built-ins. Registration metadata has its own bounded array and is not a runtime instance. Each registration now retains its canonical `BuiltInAppMetadata.appId`; aliases retain the same canonical ID.

`launchApp` and `launchAppWithParam` find the registration, construct the application, and call its initializer. Only after successful initialization, immediately before insertion into the running array, AppManager assigns the lifetime token and canonical application ID. Re-launching an already-running, unparameterized app focuses its existing window and preserves its identity.

The running array has 16 entries. `closeApp` shuts down and deletes the object, shifts later pointers toward the front, and clears the old tail. `update` similarly deletes terminated objects and compacts the array. Consequently, row positions are reused and can change when an earlier app closes. The snapshot never treats a row index as identity.

The shell is outside AppManager. `shell::open` assigns a shell-lifetime generation on a closed-to-open transition; closing makes its current identity unavailable, and a later open receives a new generation. The managed logical application is also outside AppManager. Its record uses a separate source tag and a monotonic managed-surface lifetime token.

## Identity model

Each source has a monotonically increasing unsigned 64-bit lifetime token. Zero means invalid. AppManager assigns a token once, after `init` succeeds and immediately before adding the object to the authoritative running array. Focus, redraw, refresh, and selection do not change it. A normal managed logical launch gets one token; managed input and action dispatch keep that token. Closing or replacing the managed surface clears the current token.

AppManager's running array compacts rather than retaining fixed physical slots. If A is the sole entry at row 0, A closes, and B is inserted at row 0, A's and B's tokens differ. The old token no longer resolves to a non-terminated entry. Native focused tests also exercise terminated-app removal and the full 16-entry bound.

The shell and managed-surface allocators follow the same zero-invalid, 64-bit rule. The maximum value is issued at most once; the allocator then remains exhausted and refuses further IDs. It never wraps to zero or reuses an earlier token. If managed ID allocation fails, the attempted launch fails and the still-active prior identity is preserved.

Application registration identity and runtime identity are different values. For example, `com.guidexos.apps.managed.calculator` identifies a catalog entry. A managed calculator launch has a source-tagged lifetime token in addition to that application ID. Native Calculator has `gxos.builtin.calculator`; it remains a distinct catalog entry and AppManager record.

## Snapshot content and semantics

The fixed maximum is 18 records:

| Source | Maximum | Ownership |
| --- | ---: | --- |
| AppManager instances | 16 | Native `AppManager` |
| Shell surface | 1 | Separate shell subsystem |
| Managed logical application | 1 | Existing managed surface state |

The managed logical record is explicitly tagged as a separate source; it is not counted as an AppManager instance. This capacity therefore covers all three existing application-related sources without conflating the hosted process table or kernel threads.

Records are ordered by the current compacted AppManager array order, then the open shell surface, then the active managed logical application. AppManager order is the native scan order; the kernel does not sort for UI presentation.

The native callback performs one bounded synchronous scan, builds value records in a fixed local staging array, and copies the requested prefix into the caller buffer. The current bare-metal execution path is single-core and does not mutate AppManager concurrently with this synchronous host call; no new lock or synchronization service is introduced. Snapshot calls have no state-changing side effects.

The callback reports both total and copied counts. Capacity 0 is a query-only probe: it writes no record data, returns the total count, sets copied count to 0, and reports truncation when total count is nonzero. If capacity is smaller than total, it writes exactly `capacity` records and returns truncation. Capacity above 18, null output pointers, overlapping outputs/buffer, or misaligned nonempty buffers are rejected before any output write. A null records pointer is valid only with capacity 0.

### Record layout v1

`ApplicationSnapshotRecord` is 168 bytes, aligned to 8 bytes. It contains no pointers or native object handles.

| Offset | Field | Width | Meaning |
| ---: | --- | ---: | --- |
| 0 | `recordVersion` | u32 | Record format version, currently 1 |
| 4 | `source` | u32 | 1 AppManager, 2 shell, 3 managed logical app |
| 8 | `instanceId` | u64 | Source-scoped lifetime token; zero is invalid |
| 16 | `state` | u32 | Existing `AppState` values 0–3 |
| 20 | `flags` | u32 | Bit 0 means active; other bits are reserved |
| 24 | `displayNameLength` | u32 | Bounded byte length, excluding NUL |
| 28 | `applicationIdLength` | u32 | Bounded byte length, excluding NUL; 0 means unavailable |
| 32 | `reserved0` | u32 | Must be zero |
| 36 | `reserved1` | u32 | Must be zero |
| 40 | `displayName` | 32 bytes | Fixed ASCII-compatible name buffer |
| 72 | `applicationId` | 96 bytes | Fixed canonical-ID buffer |

AppManager uses its existing 32-byte application-name bound and a new 96-byte canonical-ID field sourced from registration metadata. Both strings are currently printable ASCII, which is the bounded UTF-8 subset accepted by this ABI. Names and IDs are NUL-terminated within their fixed arrays; managed decoding uses the explicit length and never scans outside the record. Shell has the truthful display name `Terminal` and no catalog application ID.

`state` maps only the existing AppManager enum: `NotLoaded=0`, `Running=1`, `Suspended=2`, `Terminated=3`. No unobserved sleep, responsiveness, CPU, memory, or process state is inferred. Active is based on the focused compositor window for AppManager/managed records and desktop shell focus/minimized state for the shell. In the verified managed Notes and Calculator scenarios exactly one record is active. The wrapper permits zero or one active record because the native desktop may have no active application surface.

## Host ABI v2

The pre-C160 host table is ABI v1, 104 bytes. ABI v2 preserves that complete prefix byte-for-byte and appends one callback pointer:

| Version | Table size | Snapshot callback | Prefix |
| ---: | ---: | ---: | ---: |
| 1 | 104 bytes | unavailable | 104 bytes |
| 2 | 112 bytes | offset 104 | 104 bytes unchanged |

Native compile-time assertions pin the old callback offsets, the 104-byte v1 prefix, the 112-byte v2 table, and the snapshot callback at offset 104. Managed layout tests check the same table offsets, callback offset, and record size/field offsets.

The callback signature is:

```c
int32_t applicationSnapshot(
    NativeGxAppContext* context,
    ApplicationSnapshotRecord* records,
    uint32_t capacity,
    uint32_t* outTotalCount,
    uint32_t* outCopiedCount);
```

Native return values are 0 success, 1 truncated, -2 invalid argument, -3 capability unavailable, -4 invalid context, -5 unsupported ABI, and -6 invalid native state. Capability bit 11 is a read-only snapshot capability; it grants no app-management operation.

Managed code accepts host versions 1 and 2 for existing v1 functionality. It reads the tail callback only after confirming `version >= 2` and `size >= 112`, then checks the capability and non-null callback. A 104-byte v1 table returns `NotSupported` without reading offset 104. Version 2 with size 104 and version 1 with size 112 are both rejected for the feature. A v1-only synthetic 104-byte table still passes the existing prefix log call.

## Managed API and fixed memory

`GuideXosHost.TryGetApplicationSnapshot` returns a typed `GuideXosApplicationSnapshotResult` and a value-only `GuideXosApplicationSnapshot`. The result distinguishes success, truncation, unsupported ABI, missing capability, invalid data, invalid arguments, and native failure. The snapshot uses a fixed 18-record, 3024-byte aligned storage region; each record is copied out by value. There is no native pointer retention, `List<T>`, LINQ, or count-driven growth. A new call reads native state again; the copied snapshot is not an authority or shadow registry.

The native staging array costs 3024 bytes of bounded call stack. Each AppManager instance adds 104 bytes for its 96-byte application ID and 8-byte token; the 16 registration metadata entries add 1536 bytes for retained canonical IDs. Shell and managed active-token state each use fixed scalar storage. The record buffer itself is 3024 bytes on the managed side.

`GuideXosApplicationInstanceId` equality compares the source tag and 64-bit lifetime token. It never compares a row index or application ID. Thus AppManager token 42 and managed token 42 are distinct, and AppManager token 42 differs from token 43.

## Focused tests and production proof

Native AppManager tests cover ID allocation, zero invalidity, 64-bit exhaustion, admission timing, canonical IDs, stable identity through update, duplicate admission rejection, close and termination invalidation, row 0 reuse with a distinct token, 16 simultaneous slots, and capacity rejection. Native snapshot tests cover record fields, shell and managed sources, one active record, stable repeated reads, full-count metadata, zero-capacity probe, one-record truncation, oversized/null/misaligned/overlapping arguments, null output pointers, bounded strings, and 1000 repeated calls with unchanged AppManager count and shell token.

Managed proof tests cover v1 operation and graceful feature rejection, v2 acceptance, both malformed version/size combinations, missing capability/callback, exact ABI layouts, empty/one/full/truncated snapshots, bounded decoding, identity equality, invalid counts/statuses/capacity, native Calculator versus managed Calculator IDs, shell and Task Manager records, and 1000 repeated snapshots. The production proof also validates identity across each Calculator dispatch and each actual Calculator launch/close/relaunch cycle. A replacement snapshot must show the prior token absent and a distinct new token active.

The proof kernel launches the existing native Calculator and native Task Manager through their registered factories and opens the real shell surface. The Task Manager's existing row rendering is checked for the native Calculator. These proof-only diagnostics and extra startup surfaces are excluded from the canonical production kernel/composite.

Production boot 1 starts Managed Notes, verifies a real snapshot and Notes identity stability, then launches Calculator and returns to Notes. Boot 2 exercises Managed Calculator's `7 * 8 = 56` path and closes it. Boot 3 exercises Notes, Calculator, native Task Manager, snapshot dispatch stability, and 25 actual Calculator lifecycle cycles. C156 modifier/shortcut checks and C150 lifecycle/return-target checks remain part of the booted composite. C128 remains unverified; no C128 pass is claimed.

The ABI-v2 payload must replace the historical ABI-v1 managed payload for ordinary boots: the old payload's exact-v1 validation would reject a v2 host. The runner preserves the pre-C160 kernel, ESP kernel, and ramdisk hashes as historical evidence, builds a proof composite/kernel with diagnostics, runs three fresh QEMU proof boots, then separately builds a clean production composite/kernel without proof diagnostics and generates a matching production ramdisk. It installs the new kernel in both canonical kernel locations and installs the ABI-v2 ramdisk before three ordinary boots. The pre-C160 kernel hash is historical baseline, not the expected post-C160 artifact hash.

| Artifact | Result |
| --- | --- |
| Starting HEAD | `260597df` |
| Host ABI | v2, 112 bytes; v1 prefix 104 bytes; callback offset 104 |
| Proof composite SHA-256 | `8D72926775B47DF26E1A70CA93C4F6055354DC27A5E9A4B009919F56600528FB` |
| Clean production composite SHA-256 | `489D5FA81E1E6F3C4F3D6B01673D277FDD7A7C477CC7992A2661D6AA0C163428` |
| Proof kernel SHA-256 | `735AF3FFC99608556EA8C5ECEE4740829F9043C3ED047EC5DB88D9BD4A2B3E07` |
| Proof ramdisk SHA-256 | `168FA106975EC494DE2AEA41CC9D976A4A1CAFFF30F8894389DA57E45F5D758C` |
| C160 serial SHA-256, boots 1–3 | `F1C07F7201396FD4048DCD01D55E0325BB7CF367F5F937F10BBA35284A2C2BB3`; `3F86CBACC5AA77BFD700E8FC739F3D317905276534ADADA7DA3E926A03A3B42D`; `202D8ECF33BFF765DE7D95342819D63D9657D36AD3A3E1B129052C3A2EB5B5D8` |
| C159 kernel/ESP kernel SHA-256 before the first C160 install | `9C2AF5EFE44BA26DE678B3749B98BEC7180ED5A4F97A033C3976AF5CB06E3203` |
| Kernel SHA-256 immediately before this final validation pass | `0BA852C64EA33D30EAE57C5E612A25B8F4358FF85D460F23A402E41C5BD220F1` |
| Post-C160 kernel and ESP kernel SHA-256 | `0BA852C64EA33D30EAE57C5E612A25B8F4358FF85D460F23A402E41C5BD220F1` |
| C159 ramdisk SHA-256 before the first C160 install | `E26D6D7C54CBFF416CC59B8FE0E4A2B9914D15B9A74A0368EAEA27A3E103BBAE` |
| ABI-v2 ramdisk SHA-256 immediately before this final validation pass | `F037701AF60BDAE9935C4F0A5E3EBF6D616C6F72E05F292EC6C92E653A346746` |
| Post-C160 ABI-v2 ramdisk SHA-256 | `32B8D57DBD47E5DC9B95E97204A255D95593FCF4C38430C39CE812FA9F7B23D0` |
| Ordinary serial SHA-256, boots 1–3 | `D6F853FCD1A268232DBCF775A1382989F0791EED0466EE2679359C3B4DDCC565`; `B392759B774313ED0CA41892782D6638575602D0B0D9DAD147DE0C7B795B3166`; `A717260344D7D34060A8078031022AD0A0A3C12A3DCCAB7CA2B6CADB489A1F60` |
| C160 proof manifest | `out/dotnet/c160-application-snapshot/c160-proof-manifest.json` |
| Ordinary boot manifest | `out/dotnet/c160-application-snapshot/c160-ordinary-manifest.json` |

The three proof boots and three ordinary boots used QEMU WHPX acceleration. The proof suite reported 29 AppManager identity cases, 34 native snapshot cases, 22 managed snapshot cases, and 1000 native plus 1000 managed stable-snapshot calls. Managed checks include an empty typed snapshot, a one-record truncated copy, a full-capacity request, truncation metadata, and rejection of malformed count tuples. Production snapshots verified active Notes, Calculator, native Calculator, native Task Manager, and shell records. Boot 1 proved Notes instance 1 → Calculator instance 2 → a fresh Notes instance; boot 3 recorded 27 Calculator lifetime snapshots across the launch and churn sequence. The native Task Manager continued rendering its native Calculator row. The manifest reflects the final validation invocation; the separate C159-baseline rows retain the hashes from before the first C160 installation. The new ramdisk is intentional: ordinary ABI-v2 boots must use the rebuilt managed payload that accepts the extended v2 host table.

## Deferred work

C160 does not add Managed Task Manager UI, End Task, scheduler snapshots, process trees, CPU/memory accounting, termination authority, generic kernel introspection, query strings, event subscriptions, or task-change notifications. A later phase can consume this snapshot boundary without turning hosted server processes into AppManager records.
