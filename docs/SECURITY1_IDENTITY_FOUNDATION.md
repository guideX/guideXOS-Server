# SECURITY1: Identity foundation

**Repository:** `guideXOSServer_SECURITY_SETTINGS`\
**Branch:** `SECURITY_SETTINGS`\
**Status:** implementation complete; verification recorded below.

## Runtime owner and identity boundaries

`kernel/core/identity.cpp` provides the freestanding-compatible, fixed-capacity account registry. The registry is owned by `kernel::identity::Runtime`, initialized by `kernel_main` after VFS storage mounting, and queried through bounded value copies. No pointer into registry storage is returned. The hosted process table and App Model IDs remain application/process identities; neither is promoted to a human identity.

IDs are 64-bit values. Zero is invalid, ID 1 is reserved for nonhuman kernel/system operations, human IDs start at `0x1000`, and `UINT64_MAX` is the exhausted allocation watermark. IDs increase monotonically and are not reused after deletion while the allocation watermark is available. Account names are ASCII identifiers of 1–31 bytes, begin and end with an alphanumeric character, and are unique case-insensitively. Display names are printable ASCII of 1–63 bytes. Records are version 1, active or disabled, and the registry holds at most 32 accounts.

The runtime exposes an explicit kernel/system context and an interactive context with no assigned human ID. There is no operation that turns an account ID into a session or authenticated context. `AuthenticatedHuman` is a reserved context kind for a future session owner; no current code can issue it. Account existence does not mean that anyone signed in. No root account, Windows username, or default human owner is created.

The existing `kernel/core/process.cpp` process table is a scheduler stub and has no active process/session ownership contract. SECURITY1 does not add a misleading owner field there. `Runtime::queryContext` is the narrow kernel-owned query boundary for future process/session integration; current interactive callers receive `InteractiveUnassigned`.

## Persistence contract

The registry serializes a versioned little-endian record set with a generation, next-ID watermark, fixed record widths, exact-length checks, and CRC-32. The maximum encoded size is bounded at 3,404 bytes. Load rejects unsupported versions, truncated or overlong data, bad checksums, duplicate or unsorted IDs, duplicate account names, invalid lifecycle values, invalid names, and inconsistent allocation watermarks.

On a writable FAT32 root mount, the runtime stores snapshots in the reserved paths `/.gxos-identity-0.dat` and `/.gxos-identity-1.dat`. It writes the inactive slot, relies on the existing FAT close/flush path, reads the whole slot back, and validates the bytes before switching the live registry. Boot selects the newest valid generation. If a slot is malformed or an interrupted update leaves it invalid, a valid older slot remains readable, but the account count is reported unavailable and persistent writes stop with a corrupt-store status because the fallback could be stale. Unsupported formats also stop writes. Existing content at either reserved path is never silently reset.

This uses only storage behavior already present in VFS: currently writable whole-file persistence is supported for FAT32, and FAT close flushes through the block device contract. It does not provide an atomic FAT directory update or power-loss proof beyond the dual-slot and read-back checks. Other filesystems, missing disks, and read-only mounts remain supported for boot. With no writable store, controlled kernel provisioning returns `OK_VOLATILE`; Settings reports that state and records do not survive restart. Volatile IDs are only runtime references and no process/session binding survives a reboot. Persistent IDs and the allocation watermark survive restart when the FAT32 transaction commits.

## Settings integration

The native Settings `KernelApp` now exposes Users in its category list and reads a value-only identity snapshot from the native provider. The provider checks the runtime-assigned Settings App ID and reports registry availability, account count, persistence state, and the interactive context. It exposes no provisioning, authentication, or authorization operation.

The hosted Users page uses the same truthful presentation states but has no native kernel provider in the hosted runtime, so registry/current-identity information is shown as **Unavailable**. Both hosted and native pages state that authentication, profiles, permissions, file ownership enforcement, and user-granted application access are not implemented. App Model IDs remain labeled as applications, not people.

## Explicit SECURITY1 limits

- There is no password/PIN authentication, login session, account switching, or credential storage.
- Registry provisioning and lifecycle calls are kernel-internal; Settings and public service transports expose no account mutation API.
- The registry does not enforce process ownership, VFS ACLs, per-user home directories, settings profiles, application grants, or service-peer authentication.
- A record can exist while no human identity is assigned. No privilege or authenticated state follows from knowing a user ID.
- FAT32 is the only current durable identity-store path. Other writable filesystem support requires a VFS transaction/flush contract before it can claim persistence.

## Verification

- `scripts/run-identity-foundation-test.ps1`: 38/38 checks pass, covering registry lifecycle, validation, capacity, contexts, versioned serialization, restart recovery, interrupted writes, read-only/unavailable storage, stale fallback counts, and preservation of unrecognized slot data.
- `scripts/run-settings-users-model-test.ps1`: identity 13/13, navigation 8/8, interaction/layout 13/13.
- `scripts/run-settings-center-model-test.ps1`: 89/89 checks pass.
- `scripts/run-settings-native-layout-test.ps1`: 1,037/1,037 checks pass, including Users category availability and 1,000 resize geometries.
- `scripts/run-settings-native-runtime-test.ps1`: 1,432/1,432 checks pass, including the identity provider’s live empty/unassigned state, version rejection, and caller authorization.
- AMD64 freestanding build: passes and produces `kernel/build/amd64/bin/kernel.elf`.
- Hosted full `build.bat` build: passes and produces `guideXOSServer.exe`; existing warnings remain in unrelated code.
- No QEMU guest was started. Build and native provider/layout tests exercise the new runtime contracts without taking shared guest ownership.

## Recommended SECURITY2 scope

Start with a kernel-owned session manager and a binding token that only that manager can issue, revoke, and validate. Define sign-in and recovery only after selecting a credential format and cryptographic key-management path. Then bind a validated session reference to real process ownership records and define authorization checks before adding user-facing account controls. Treat filesystem ownership/ACLs, user-scoped preferences, application grants, and host/service-peer authentication as separate enforcement work with explicit migration and compatibility plans.
