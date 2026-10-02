# C159 Managed Task Manager: snapshot boundary audit

## Outcome

**Outcome C — the managed runtime has no truthful read-only system snapshot
boundary.** The source audit found authoritative native application/runtime
state, but the production NativeAOT host-call table does not expose it to C#.
The table is already full at 104 bytes. The C159 instructions say to stop and
document the missing operation when no safe existing call or table slot exists;
therefore this phase adds no managed application, app descriptor, or ABI slot.

This is an architecture finding, not a claim that a managed Task Manager was
built or validated. No rows are synthesized from launch context, launcher
catalog entries, or the native Task Manager's rendered UI.

## Existing native Task Manager audit

There are two native implementations with different execution models.

### Bare-metal kernel application (the C159 production target)

- App ID: `gxos.builtin.taskmanager`.
- Launch identity: `TaskManager`; visible label/title: `TaskManager` / `Task
  Manager`.
- Registered by the built-in application metadata and `AppManager`; its class
  is `kernel::apps::TaskManagerApp` in `kernel/core/kernel_apps.cpp`.
- Its Processes tab actually enumerates `AppManager::getRunningAppCount()` and
  `getRunningApp(index)`, then optionally adds the open shell as `Terminal`.
  It does not enumerate scheduler threads or `kernel::process::Process`
  records. Its own detail text calls these rows applications and shell
  windows.
- Each row contains only a bounded app name, `running`, `isShell`, and a
  `windowCount` field set to the constant `1`. The UI labels that field
  “Windows”; it is not a measured window count. It has no PID, app ID, kind,
  or app-instance generation in its row record.
- `refreshList()` caps rows at 16 (`MAX_ENTRIES`). If all 16 app slots are
  occupied, an open shell is omitted without a truncation indicator. Selection
  is an integer row index; refresh only clamps it to the last row. It does not
  preserve identity, so a different app can inherit the old selection at the
  same index.
- The Refresh widget calls `refreshList()`. `update()` also refreshes by an
  update-call counter (100 calls on the Performance tab, 200 elsewhere), not
  by a wall-clock timer.
- The End Task widget can call `AppManager::closeApp()` or `shell::close()`.
  C159 must not reproduce that authority.
- Its Performance and Memory Details tabs show system-wide kernel telemetry:
  CPU from `kernel::desktop::cpu_telemetry_snapshot()` and global heap totals
  from the kernel heap accessors. The kernel heap is documented in the UI as a
  1 MiB bump allocator. These are not per-application CPU or memory counters.
  The list does not show per-app resource values. The Tombstoned tab explicitly
  says bare metal does not collect app tombstones.
- The app list comes from fixed `AppManager` arrays with `MAX_APPS = 16` and
  `MAX_APP_NAME = 32`. `AppManager` stores app pointers and names, not stable
  instance IDs or generations. Closing an app compacts the array, so row/index
  is not stable identity.

The native list is copied synchronously from `AppManager` into the Task
Manager's 16-entry array. There is no snapshot service, version counter, or
documented snapshot lock. Kernel app launch/close and UI update are currently
serialized through the single-core application path, but that native detail
does not create a managed access contract.

### Hosted/server implementation (a separate model)

The hosted `gxos::apps::TaskManager` uses `ProcessTable::list()` as its primary
row source. `ProcessTable` owns a mutex-protected unordered map of `Process`
objects. `spawn()` assigns monotonically increasing 64-bit PIDs starting at
10, stores a process name and optional app ID, and runs the entry on a detached
host thread. A process record remains until `terminate(pid)` erases it. The
hosted manager joins optional `NativeAppProcessTable`, compositor window, and
allocator-per-PID data to those PIDs. Its reported memory bytes come from
`Allocator::pidBytes`; process CPU is derived from host thread CPU-time samples
and a wall-clock sample window. System CPU, scheduler executed-task count,
global heap, disk, and network telemetry have separate sources. Disk and
network per-process percentages are explicitly unavailable.

The hosted scheduler is a worker queue of callback `Task` values, not the
hosted application's process table. It exposes an aggregate executed count
and aggregate idle/busy CPU telemetry, but no task enumeration API. The
hosted native Task Manager's process registry and host-thread data are not
the bare-metal kernel `AppManager` inventory and are not callable from the
production NativeAOT app.

The optional `NativeAppProcessTable` is an experimental native ELF runtime
diagnostic table. It assigns a runtime ID and records lifecycle/diagnostic
fields, but `List()` returns no records when experimental native ELF execution
is disabled. It is not the managed NativeAOT logical-app inventory.

## guideXOS object terminology and ownership

“Task Manager” is a product name. For a truthful C159 row model on bare metal,
the relevant unit is an **application instance** (and, separately, the shell),
matching the native kernel Task Manager's actual source. These are not Windows
or Linux processes.

The codebase has several distinct concepts:

| Concept | Owner and available source | Why it is not the C159 snapshot |
|---|---|---|
| Kernel GUI application instance | `kernel::app::AppManager`, up to 16 live `KernelApp*` entries; current native Task Manager reads this array | C++-only pointer/name enumeration; no public stable instance ID or managed host call |
| Shell | `shell::is_open()` and `shell::close()` | Separate boolean object; no shared app-instance identity or managed snapshot call |
| Kernel process record | `kernel::process::Process`, fixed 16-record array; `create_init_process()` assigns PID `process_count + 1` | Not the GUI app registry; the public header has no process-list/snapshot function |
| Kernel scheduler thread | `kernel::process` has 16 thread slots on AMD64; TCBs have TID, owner PID, name, `ThreadState`, and a slot generation | No production enumeration API. The exposed test snapshot/live-count accessors exist only under `GXOS_NATIVE_THREAD_QEMU_TEST` |
| Hosted server process | `gxos::ProcessTable` and its detached host-thread entries | Separate hosted/server implementation, not the bare-metal App Model state |
| Managed logical application | Static `BuiltInAppMetadata` launch descriptors plus `nativeaot_application.cpp`'s active app ID and launch generation | Descriptors describe launchable apps, not running instances. Active ID/generation are kernel-private and not in the managed host table |

`AppManager` is the owner of the bare-metal running GUI app inventory. The
managed NativeAOT App Model separately owns its active logical app ID and
launch generation. The managed catalog is launcher metadata, not a second
runtime registry and not a substitute for a live snapshot.

## Managed access and ABI audit

`GuideXosHost` gives managed code a copied launch context (selector, flags,
bounded launch text) and callbacks for UI surface/drawing/buttons, close,
actions, logging, and bounded file read/write/list/stat. It exposes no
current-app query, AppManager enumeration, shell state, scheduler/thread
snapshot, process snapshot, generation query, or per-app resource query.
`g_managedActiveApplicationId` and
`g_managedApplicationLaunchGeneration` are kernel-side implementation state;
the C150 inspection helpers are proof-harness functions, not managed host
calls. No existing read-only system-service/App Model path supplies the
required snapshot to C#.

The current native host-call table is ABI version 1 and 104 bytes:

| Offset | Field |
|---:|---|
| 0 | `size` (`uint32`) |
| 4 | `version` (`uint32`) |
| 8 | `log` |
| 16 | `requestWindow` |
| 24 | `drawText` |
| 32 | `drawRect` |
| 40 | `addButton` |
| 48 | `closeWindow` |
| 56 | `capabilities` (`uint64`) |
| 64 | `addActionButton` |
| 72 | `fileReadAll` |
| 80 | `fileWriteAll` |
| 88 | `directoryList` |
| 96 | `fileStat` |
| 104 | End of table; **no spare or reserved slot** |

The managed `NativeHostCallTable` and kernel `NativeHostCallTable` definitions
match this layout; the kernel asserts size 104 and the `fileStat` offset 96.
The launch context is 40 bytes and its copied text is limited to 48 bytes; it
is input/launch transport, not an appropriate place to smuggle a live task
snapshot.

**ABI decision: unchanged (v1 / 104 bytes).** Reusing a file, UI, action, or
launch-context field would violate the existing contract. The smallest
reasonable future addition would be one append-only, capability-gated,
caller-buffer snapshot operation, with a fixed record version, capacity,
returned count, and explicit `hasMore`/truncation result. It would have to
copy bounded data in kernel code and return no pointers. Appending one pointer
would extend the table to at least 112 bytes at offset 104. The old prefix can
remain ABI v1 as earlier table extensions did, but both declarations,
capability negotiation, host validation, and ABI negative/boundary tests must
be updated. Old clients must continue to use the 104-byte prefix; the new
managed app must reject a host without the new field/capability. A new host
must not require the new larger full struct from legacy clients.

## Capacity, identity, consistency, and data fields

No managed snapshot capacity or record layout can be assigned truthfully
because the operation does not exist. Relevant native bounds are:

- `AppManager`: 16 registered-app slots and 16 running-app slots.
- Bare-metal native Task Manager: 16 display entries; a separate open shell
  can be an additional object and is silently left out when the 16 app entries
  are full.
- Kernel process table: 16 process records, currently without public
  enumeration.
- AMD64 kernel thread slots: 16, with an internal per-slot generation and
  monotonic TID assignment; production enumeration is absent.
- Managed catalog: compile-time metadata array, distinct from running-app
  capacity. The supplied C158 summary reports three production descriptors.
  The source also retains Workspace, Status, Counter, and Notes launch rows,
  with Settings Center and Calculator rows controlled by compile definitions.
  The raw source row count and the phase's production-descriptor count use
  different scopes; this phase did not build to recalculate the configured
  count. No C159 descriptor was added.

The native bare-metal Task Manager exposes no app ID, instance ID, or
generation to preserve. Its row index is unsafe across refresh. The kernel
managed App Model does have a launch generation for managed logical-app
transitions, but it is not a generation for every `KernelApp` instance and is
not passed to C#. AppManager's pointer array compacts on close and does not
assign a stable token. Therefore C159 cannot prove ABA-safe selection from the
existing managed boundary. This is part of the missing contract; no claim of
ID-reuse safety is made.

The kernel process table uses monotonically assigned PIDs and the kernel
thread table uses monotonically assigned TIDs plus slot generations. These
are distinct object classes and cannot be presented as GUI app identity
without an explicit mapping. The hosted `ProcessTable` has monotonic PID
assignment but no generation field; ordinary ID reuse is not part of its
current design, though the counter has no wraparound policy. No row from any
of these tables is available to C# today.

The existing bare-metal Task Manager's application-name order is current
`AppManager` array order, which changes when an app closes. It has no stable
ID sort. A future copied snapshot should return a deterministic order and a
per-lifetime stable identity, and preserve selection by that identity. For
native apps, that requires assigning an instance ID/generation in the app
owner; for the managed surface, it also requires copying its canonical app ID
and logical launch generation. If the copied source is a separate kernel
thread snapshot, its identity should use TID plus slot generation and it must
remain labeled as a thread.

No trustworthy per-app memory or CPU percentage is exposed to managed code.
The native Task Manager's kernel-wide CPU/heap values cannot be relabeled as
per-app values. C159 therefore must omit per-app memory and CPU, not estimate
them. It must also omit process semantics, task state, active-app indicators,
and Native/Managed/Kernel kind unless a snapshot service returns those fields
authoritatively. The managed catalog alone can distinguish launchable
descriptors, not prove a particular instance is running.

## Managed UI and registration status

No `com.guidexos.apps.managed.taskmanager` record or “Managed Task Manager”
launcher label was added. The native app ID and launch identity remain
unchanged. No ListBox, detail Labels, Refresh/Close controls, focus order,
Ctrl+R route, selection behavior, viewport, refresh error handling, or
truncation indicator was implemented. There is no managed control count or
managed list capacity for C159 to report.

For the C158 starting configuration, the requested baseline remains Start
entries 16 and All Programs 19. No C159 entry was added, so both remain
unchanged. `AppManager` capacity remains 16. No second task registry was
created. Clipboard, Notes, both Calculator implementations, the shared
managed surface model, and the existing native Task Manager were not changed.

## Validation and evidence classification

The source audit covered `task_manager.h/.cpp`,
`kernel/core/kernel_apps.cpp`, `kernel/core/include/kernel/kernel_apps.h`,
`kernel/core/kernel_app.cpp`, `kernel/core/include/kernel/kernel_app.h`,
`kernel/core/process.cpp`, `kernel/core/include/kernel/process.h`,
`scheduler.h/.cpp`, `native_app_process_table.h/.cpp`,
`built_in_app_metadata.h`, `kernel/core/nativeaot_application.cpp`, its public
header, and the managed ABI/host/launch-context sources.

| Check | C159 status |
|---|---|
| Snapshot core tests (16+) | Not run; no snapshot contract exists |
| UI integration / ListBox / Refresh / Ctrl+R | Not run; no managed app was implemented |
| Lifecycle / refresh / churn / ID-reuse tests | Not run; no managed snapshot path exists |
| Snapshot failure recovery | Not run; no snapshot operation exists to inject |
| NativeAOT composite build and hashes | Not run; implementation stopped at the explicit ABI gate |
| C159 production QEMU boots 1–3 and serial hashes | Not run; no C159 proof build was created |
| Ordinary artifact restoration and three ordinary boots | Not needed; no proof artifacts were built or installed |
| C158 arithmetic/registration/lifecycle/stress | Historical C158 baseline from the supplied brief: arithmetic 39/39, registration 6/6, lifecycle 25/25, arithmetic stress 160 commands, focus/input stress 150 events. Not rerun in C159. |
| C157 Notes / clipboard smoke | Not rerun; no shared application code changed |
| C156 modifier decoder / shortcut routing | Not rerun; C159 added no shortcut handler |
| C137 ListBox 46/46 | Not rerun; no ListBox integration was added |
| C150 return-target 10/10 and lifecycle 8/8 | Historical evidence only; not rerun |
| C129 31/31 and Tab / Shift+Tab | Historical evidence only; not rerun |
| C128 | **Unverified**; no C128 completion marker was collected here |

No current QEMU serial state, final active app, selected item, Control/Shift
state, modal owner, popup capture, drag owner, C159 manifest, C159 serial hash,
composite ELF hash, proof-kernel hash, or protected-ramdisk hash was collected
in this audit. Existing ordinary kernel/ESP/ramdisk artifacts were not
modified. Settings remain format v2 according to the C158 baseline; this
phase made no settings change.

## Exact blocker and next boundary

The missing operation is a **bounded, read-only running application-instance
snapshot from the bare-metal `AppManager`/managed App Model to managed code**.
At minimum it must provide count, caller capacity, explicit truncation,
stable per-instance identity/generation, canonical app ID or bounded app
name, real kind/state, and the current managed app identity/generation when
applicable. A coherent copied snapshot must not return `KernelApp*`, pointers
into catalog strings, scheduler TCBs, or other native-owned memory. The
managed side must have a fixed record limit and bounded string storage.

Before implementing that contract, the kernel side also needs to decide how
native `KernelApp` instances receive stable IDs/generations, how the shell is
represented, how the managed logical app generation maps to the persistent
`NativeAotManagedSurface`, and what synchronization makes one copied view
coherent. A separate kernel-thread inventory is not a replacement for an
application inventory. Per-app memory and CPU fields should remain absent
unless their ownership/accounting semantics are separately established.

The ABI was deliberately left at **version 1, 104 bytes**. Reopen C159 only
after the host API owner approves and specifies the new bounded snapshot
contract and its append-only compatibility rules. Until then, Outcome C is the
truthful result; no fake task rows, duplicate registry, termination
authority, CPU percentage, or memory value has been introduced.
