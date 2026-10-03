# C161 Managed Task Manager

## Outcome

**Outcome A — production Managed Task Manager validated.** The app is registered through the ordinary managed App Model and reads live application-instance data only through the C160 bounded snapshot wrapper. It displays real records, refreshes by stable C160 identity, survives failure and churn, and passed three proof boots plus three ordinary boots.

## C159 blockers resolved

C159 stopped at Outcome C because managed code had no truthful read-only AppManager snapshot, no stable instance token, and no safe way to preserve a selected row across array compaction. C160 resolved all three: it added the bounded ABI-v2 snapshot callback and managed wrapper, source plus unsigned 64-bit lifetime IDs, and identity equality. C161 consumes those contracts as-is. No additional ABI field or app registry was added.

The data path is:

```text
Managed Task Manager UI
-> GuideXosHost.TryGetApplicationSnapshot
-> ABI v2 callback (offset 104)
-> AppManager / managed logical surface
```

The 104-byte ABI-v1 prefix remains intact. The production table is v2 and 112 bytes. C161 adds no host call; it performs the version, size, capacity, and result validation through the existing wrapper.

## Registration and bounded UI

| Item | Before C161 | After C161 |
|---|---:|---:|
| Managed runtime descriptors | 3 | 4 |
| NativeAOT metadata catalog | 6 | 7 |
| Start entries | 16 | 17 |
| Start pinned entries | 15 | 16 |
| All Programs entries | 19 | 20 |

The canonical ID is `com.guidexos.apps.managed.taskmanager`, selector 7, display name **Managed Task Manager**. The native `gxos.builtin.taskmanager` and its existing launcher remain intact. The C158 shared ControlHost limit remains 20; Task Manager registers three controls in this order: Application list, Refresh, Close. Its own application control capacity is three.

The C160 snapshot is the authority for both contents and ordering. Capacity is 18 records (16 AppManager entries, one shell surface, and one managed logical surface). `GuideXosListBox` is also configured for all 18 rows. Each bounded row label has 48 UTF-16 characters, for 864 characters / 1,728 bytes of row text storage. Refresh copies into fixed record and row buffers; it does not grow a collection or create a new row-string graph per snapshot. The UI uses a single list and bounded detail Labels within the normal application surface.

Rows show the C160 name and source. Details show the canonical application ID when present, full fixed-width 16-digit hexadecimal lifetime ID, the C160 state enum, and the authoritative active flag. Shell is displayed as **Terminal**, source ShellSurface, without an application ID or process claim. Native and managed Calculator records keep their distinct IDs (`gxos.builtin.calculator` and `com.guidexos.apps.managed.calculator`). The one-active-surface model is reported as observed: replaced surfaces are not described as still running. No kind classification beyond the actual C160 source is invented.

CPU, memory, process/thread data, and task states outside the C160 enum are omitted. There are no kill, end-task, suspend, resume, activate, or other mutation controls. Enter does not trigger an action on the selected row. Settings remain format v2, with no Task Manager persistence.

## Selection and interaction

Selection identity is the existing `GuideXosApplicationInstanceId`: C160 source plus the full 64-bit lifetime token. Row index, name, and app ID alone are never used as identity. Snapshot ordering is preserved. Refresh makes one new wrapper call, validates the result, rebuilds the bounded rows, and keeps the prior selection only if that exact identity remains. A disappeared identity clears details and selection; a reused AppManager slot with a new lifetime ID cannot inherit the old selection. A surviving selection is revealed in the viewport; otherwise the viewport returns to a valid origin.

The button and Ctrl+R route call the same refresh method. Control-modified `r` is consumed so it does not leak as text, and the existing host shortcut priority rules apply. Pointer selection updates details immediately. Up/Down and Home/End use the existing ListBox navigation. Tab and Shift+Tab follow the registered list → Refresh → Close order and wrap through the existing host focus path. Wheel input uses the shared ListBox NaturalScroll and ScrollLinesPerNotch policy; no Task Manager wheel arithmetic was added.

On snapshot failure, the bounded error status is shown and the last valid rows/details remain. If the first snapshot fails, the list stays empty with the failure status, rather than presenting a fabricated authoritative empty state. Truncation is visible as “Showing copied of total applications” using the actual counts. ABI-v1/unsupported snapshot results are rejected by the wrapper before callback dereference and follow the same bounded failure behavior.

Each launch performs a fresh initial snapshot and has no cached prior rows or selection. The C161 proof build checks shared clipboard contents at Task Manager launch and Refresh; both preserve `GuideXosClipboard.Shared`.

## Verification

Focused C161 UI/snapshot coverage: **24 cases PASS**, including initial/empty/one/max rows, truncation, details and active state, reordered identities, disappearance and slot reuse, pointer/keyboard behavior, viewport retention/clamping, Ctrl+R and text suppression, wrapper errors and recovery, registration/lifecycle, and bounded refresh behavior. A 1,000-call C160 wrapper stress passed. Existing C160 regressions passed: AppManager identity **29/29**, native snapshot **34/34** plus 1,000-call stress, managed snapshot **22/22** plus 1,000-call stress, ABI-prefix/malformed-table fixtures, and snapshot records for shell, Calculator, Task Manager, Notes, and managed logical application.

The production Task Manager observed itself as active, with one active record in the final 11-record snapshot. Its lifetime ID remained stable through 100 refreshes. The final selected record was native Calculator `gxos.builtin.calculator`, lifetime `0000000000000013`, state Running, Active No; its detail pane matched the selected identity. Boot 3 executed 125 real Ctrl+R refreshes and 25 close/relaunch lifecycle cycles; all 25 new launches had unique lifetimes and every close cleared the three control registrations. A QMP pointer selection and wheel-down changed the Task Manager ListBox selection/viewport.

| Regression / scenario | Result collected in C161 runner |
|---|---|
| Managed Calculator | C158 focused routing/controls and production arithmetic smoke passed; `7 * 8 = 56` |
| Notes / clipboard | C157 New-document suite and production Ctrl+N, edit, Copy, Task Manager transition, and clipboard-preservation checks at launch/Refresh passed; the C154 Paste suite passed separately |
| C156 keyboard | modifier decoder 10/10; shortcut routing 15/15; modifier balance passed |
| C137 scrolling | real QMP wheel moved the Task Manager ListBox using shared policy; standalone C137 46/46 is not claimed |
| C150 lifecycle | lifecycle 8/8 and return target 10/10; managed Calculator and Notes replacement path passed |
| C129 focus | production Task Manager and Calculator Tab/Shift+Tab passed, with Shift released; standalone 31/31 is not claimed |
| C128 | **Unverified**; no completion marker or result is claimed |

Production proof boots, all on QEMU WHPX:

| Boot | Scenario | TM launches / closes | Ctrl+R refreshes | Serial SHA-256 |
|---:|---|---:|---:|---|
| 1 | pointer/arithmetic | 1 / 1 | 6 | `8E8491BE82DA90047847F90FA701CE91E847E6CA40C4DAD601C05C3A8912426F` |
| 2 | keyboard/focus | 2 / 2 | 20 | `0FC22781A99EBADB9D375F9DFCBD99282730254F4D0A8A4070ECC178316BA141` |
| 3 | focused error/recovery fixture plus production lifecycle | 26 / 25 | 125 | `6E44A9A81A3D8C6EA058052F708765520E8F325F9E3AD549BE891C1146F942D1` |

The final proof snapshot has 11 records and one active Managed Task Manager record. Task Manager lifetime is decimal 55 (`0000000000000037`). The active application is `com.guidexos.apps.managed.taskmanager`. Three controls are registered; Control and Shift are released and balanced; modal owner, popup capture, and drag owner are all none.

The clean NativeAOT composite built with zero errors. Proof-only instrumentation is excluded from the clean composite and production kernel. ABI is v2 / 112 bytes (104-byte v1 prefix; snapshot offset 104); Settings is v2; no new host call, reflection, floating-point Calculator support, or unbounded collection was added.

| Artifact | SHA-256 |
|---|---|
| Clean production composite ELF | `B3A332E31C88175904FA13559B88CFA1CE4EFF3DF8E221F564D3E3D854C20FAD` |
| C161 proof kernel | `62C53C8B6A4808F53F48C7CD582BD4626026FFBCC23838A926F943E8A830FA7C` |
| C161 proof ramdisk | `A35B8DB6E9FF194C19E70979FFA6701C5CFA3E42D7BF26DBE266714A61738949` |

The three ordinary post-phase boots passed with the clean production kernel and ABI-v2 ramdisk:

| Boot | Active app | Serial SHA-256 |
|---:|---|---|
| 1 | Managed Task Manager, lifetime 2 | `AD9533E53EC12D4772E52AEA835807A2BC460E1346ECFAD2249745E6179F877E` |
| 2 | Managed Task Manager, lifetime 2 | `2E1404EAE48182319F0C356240841AC7314B5B84D1BA48B4E38A5C4166309092` |
| 3 | Managed Task Manager, lifetime 2 | `8B0A08783AEDD289A71C01D402EB7C99CB5BDFBF7374A814EB603B8BC946D501` |

The authoritative C160 starting kernel and ESP kernel hash was `0BA852C64EA33D30EAE57C5E612A25B8F4358FF85D460F23A402E41C5BD220F1`; the starting ABI-v2 ramdisk hash was `32B8D57DBD47E5DC9B95E97204A255D95593FCF4C38430C39CE812FA9F7B23D0`. After C161, `kernel/build/amd64/bin/kernel.elf` and `ESP/kernel.elf` match at `60549117A0E091640ED6D22A7CCE0265E2ACE0A222EAA17DDA7187A89DB0E088`. The intentionally updated ABI-v2 ramdisk is `022BF451C5397CD0FF4699DC9EF9202B7F953669AEDDB76CAF8C3E1462FAEDD0`. Proof media stayed isolated from these canonical products.

Machine-readable proof and ordinary manifests, plus serial captures, are generated under `out/dotnet/c161-managed-task-manager/` and are ignored build evidence. The runner is `scripts/dotnet/run-c161-managed-task-manager.ps1`.

## Deferred

C161 does not add task mutation, CPU/memory accounting, scheduler-thread or hosted-process views, search/filter, sorting UI, clipboard export, timers, or change notifications. It remains an observer of the C160 contract; row position is never identity and no second application registry exists.
