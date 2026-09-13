# Developer Studio Phase 28K: Multiple Persistent Source Breakpoints

Phase 28K adds a bounded source-breakpoint manager to the native ELF debugger. A debug session may own up to `GX_DEVELOPMENT_DEBUG_MAX_SOURCE_BREAKPOINTS` entries (currently eight). Each entry has a stable session-scoped ID, source identity, resolved address, enabled state, installed state, condition metadata, and hit counters.

## Protocol

The existing request prefix remains unchanged through `expression` at 104 bytes. The request is append-only and can include:

- `sourcePath`, `sourceLine`, and `sourceColumn` for source-breakpoint operations;
- `sourceCondition` for an optional condition expression.

The complete request is 136 bytes. Clients must set `request.size` to the structure size they provide; the host copies only fields covered by that size, preserving older clients.

Commands 22–26 are:

| Command | Effect |
| --- | --- |
| `ADD_SOURCE_BREAKPOINT` | Resolve and add one source breakpoint. Returns its stable `breakpointId`. |
| `REMOVE_SOURCE_BREAKPOINT` | Remove one breakpoint by stable ID and restore its byte if installed. |
| `LIST_SOURCE_BREAKPOINTS` | Return the deterministic ID-sorted bounded record list. |
| `ENABLE_SOURCE_BREAKPOINT` | Enable and install one breakpoint when the image is live. |
| `DISABLE_SOURCE_BREAKPOINT` | Disable and restore one breakpoint without deleting its identity. |

Operation results are in `breakpointOperationStatus`: `SUCCESS`, `INVALID`, `CAPACITY`, `DUPLICATE`, `CONFLICT`, `NOT_FOUND`, `STALE`, or `PATCH_CONFLICT`.

Each list record reports the stable ID, session generation, target address, enabled/installed state, source mapping, condition hash/length, and hit/false-hit/true-hit counters. The snapshot list is appended after the Phase 28J conditional evidence and is bounded to eight records.

## Runtime invariants

The physical patch manager owns one exact byte per installed breakpoint. It captures the original byte before writing `INT3`, verifies that an address is not already owned, preserves the page permissions, flushes instruction state, and restores only the requested address. Session teardown restores all remaining patches before unmapping the image.

At a trap, RIP is normalized to the trapped `INT3` address. Only that address is temporarily restored for instruction execution. A conditional false hit rearms only the servicing breakpoint after the single-step trap; all other enabled breakpoint patches remain installed. A true hit leaves the debugger paused with the servicing breakpoint identity in the snapshot. Step Over/Out and resume use the same exact-address ownership checks.

Source breakpoints are resolved against the loaded image’s source mapping. Duplicate source locations and physical address collisions are rejected deterministically. Removing or changing the loaded image invalidates the old session generation, so stale IDs or image identities cannot mutate a new session.

The legacy entry-point breakpoint path remains available when no user source-breakpoint list is configured. Existing command-21 debugger clients continue to use the unchanged request prefix and snapshot prefix.

Step Over and Step Out keep their temporary return controls separate from the persistent user table, but use the same loader-owned address registry. If a temporary return address is already owned by a persistent breakpoint, the temporary install is rejected rather than saving `0xCC` as a second “original” byte. The operation therefore fails with its existing deterministic step error and leaves the user breakpoint intact.

Completion, cancellation, release, and failure restore/retire the complete set before the image mapping is torn down. A new run starts with an empty user table and a new generation; breakpoint IDs are not reusable across generations.

## Verification

Host ABI layout and manager-contract tests:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-native-abi-layout-test.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-native-breakpoint-manager-contract-test.ps1
```

The fresh-boot QEMU proof covers fixture compilation, seven simultaneous source breakpoints, deterministic duplicate and capacity rejection, disable/enable, conditional false and true hits, per-entry rearm, removal, GUI cleanup, and the legacy run path:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-compiler-bootstrap.ps1 `
  -Phase28KOnly -BootCount 3 -TimeoutSeconds 180
```

Expected terminal evidence includes `DEVELOPER_STUDIO_PHASE28K_PASS` once per fresh boot, plus the add/list/duplicate/capacity, three-active, false-hit/rearm/true-hit, independent source hit, disable/enable/remove/stale-ID, render/close/cleanup, and legacy regression markers. The Phase 28K fixture uses the warm compiler cache on its debug run and includes source locations in both `src/main.cpp` and `src/helper.cpp`; the standalone Developer Studio GUI is not required to be mouse-driven for this proof.

The object ABI remains 10: breakpoint management is runtime/debugger state and does not require compiler metadata changes.
