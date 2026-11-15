# SECURITY3B — Native Authorization and Administrator Enforcement

**Project:** guideXOS Server
**Branch:** `SECURITY_SETTINGS`
**Outcome:** `SECURITY3B_NATIVE_AUTHORIZATION_VERIFIED` — scoped native API enforcement
**Starting baseline:** `de082141c6964843f1558ed0f82d9ca616054faa`

## Scope and result

SECURITY3B adds a kernel-compatible authorization policy tied to the native authentication runtime. Account administration now requires an active Administrator session at the mutation service, the Settings provider exposes only kernel-assigned Settings app routes for account actions, and the identity runtime stops accepting direct registry mutations after the authentication manager binds it.

The VFS also protects the four identity and credential snapshot paths from ordinary VFS write, create, delete, directory, and rename operations. The native identity and authentication storage adapters use a separate internal writer for verified snapshot commits.

This is a scoped authorization system. Applications and the native ELF loader still run with kernel privilege, there is one global interactive session, and the process table has no reliable human-user ownership. The implemented API checks do not provide isolation from arbitrary code already executing inside the kernel.

## Authorization model

`kernel/core/include/kernel/authorization.h` defines typed operation identifiers for account, application, process, and filesystem actions. It also defines context kinds, authorization decisions, and separate diagnostic reasons. Unknown operations and unsupported contexts fail closed.

Production account decisions use `authentication::Runtime::authorizeCurrent()`. It obtains the active identity from the session manager, revalidates that session against the current identity and credential state, and reads the account's current role. It does not accept a caller-supplied user ID, role, session reference, App Model ID, or process ID as the caller identity. The target user ID is used only for self-service comparison or as the object of an administrator operation.

No authorization decision is cached. A role change is read on the next decision. Credential changes, credential revocation, account disabling, and account deletion invalidate the affected session. Sign-out clears the interactive identity. Expiration by elapsed time is not implemented.

The policy model includes an explicit trusted-system context, but `authorizeCurrent()` never accepts a context object from its caller. The authentication runtime owns the trusted internal mutation paths. The pure policy evaluator is not itself an authorization grant.

The runtime retains bounded, nonsecret audit diagnostics: saturating decision and denial counters plus the last operation, decision, and reason. It stores no credential, verifier, password, session reference, or user-supplied display text in the audit record.

## Account operations

The authenticated service enforces these operations:

- Create an additional account as a Standard User.
- Enable or disable another account.
- Change another account's role.
- Delete another account.
- Reset another account's credential.
- Enroll a credential for an account when authorized.
- Change or revoke the current user's credential after proving the current password where required.

Standard Users cannot create accounts, reset another account's credential, change roles, disable or delete accounts, or promote themselves. Self-service credential authorization also checks that the target ID equals the session manager's current user ID. The existing last-Administrator protections remain active.

The identity runtime's public `provision`, `setStatus`, `setRole`, `erase`, and `initialize` operations reject calls after the authentication manager binds it. Only the friend authentication runtime can use the private mutation paths. This prevents an application from bypassing the role check by calling the lower-level registry API or rebinding the registry after authentication has initialized.

The Settings provider's account wrappers additionally require the AppManager-assigned `gxos.builtin.settings` runtime app ID. The Settings Users page displays the current account and role, whether account administration is permitted, that account administration is implemented, and that broader system authorization remains incomplete. It continues to allow sign-in and sign-out without requiring sign-in at boot.

## Bootstrap and recovery

Bootstrap requires a readable identity registry and no Administrator account record. This supports an empty first-boot registry and the SECURITY2 recovery path for an existing Standard User account. A populated registry cannot use bootstrap to create an arbitrary account; an existing credential must be proven before its account is promoted. SECURITY2's local unavailable-storage compatibility mode is preserved: first-admin setup may succeed as `OkVolatile` when the configured runtime has no storage adapter, and the snapshot reports the credential as non-durable. A configured store that reports corrupt, unsupported, or unavailable data does not qualify as this no-adapter mode.

An Administrator record blocks bootstrap even if that record is disabled or has no usable credential. Thus an inaccessible or disabled last Administrator does not reopen anonymous setup. The identity registry independently preserves its last active Administrator safety checks.

The native Settings provider also checks the runtime Settings app identity and the authoritative `bootstrapRequired` state. Boot remains in unassigned compatibility mode when no session is active.

## Enforcement inventory

| Operation or boundary | Status | What is enforced | Limit |
|---|---|---|---|
| Create account, change role/status, delete account, reset credential | **Authorization enforced at native service API** | Live Administrator session and current account role are checked at the authentication mutation service; direct identity-runtime mutation APIs are closed after binding. | No per-process caller identity or privilege separation exists between kernel applications. |
| Self-service credential change/revocation | **Authorization enforced at native service API** | The session user must match the target; password change verifies the current credential; successful changes invalidate the session. | One global interactive session; no per-process session binding. |
| First administrator setup and recovery | **Narrow bootstrap policy enforced** | Requires a readable identity registry and no Administrator account record; only an existing account can be recovered in a populated registry. SECURITY2's explicit no-adapter path remains volatile. | Configured corrupt/unavailable stores fail closed; volatile setup is not durable. Settings app identity is an in-kernel API check, not a user-mode boundary. |
| Identity and credential VFS snapshots | **Internal API check** | Generic VFS mutation APIs reject normalized reserved paths, including case variants and VFS aliases. Native storage adapters can commit through their internal writer. | Direct filesystem-driver, block-device, or arbitrary kernel access can bypass this API check. No FAT32 ownership or ACLs are claimed. |
| Application install/remove, trust policy, system registrations | **Unimplemented** | Operation IDs are reserved in the policy model. | There is no authoritative package installation/removal control point in the current runtime. Launch behavior remains unchanged. |
| Application runtime isolation | **Unimplemented** | No application is blocked by a blanket signing rule. | Native applications and ELF code execute with kernel privilege; App Model identity is not a human-user identity. |
| Process termination by user or process owner | **Unimplemented** | No owner is inferred from a process name or App Model ID. | The process table is a scheduler stub without authenticated human-user ownership. |
| General protected configuration and administrative file operations | **Unimplemented** | Ordinary VFS behavior and Disk Manager remain available. | Only the fixed identity/credential snapshot names receive the VFS API check; there are no persisted per-user file owners or ACLs. |

## Trust-boundary limitations

The AMD64 runtime has no user-mode application boundary or per-application credentials. `KernelApp` runtime identity is useful for routing Settings operations, but it does not isolate one kernel application from another. An application already executing with kernel privilege can call internal kernel APIs and can act with the globally active interactive Administrator session. SECURITY3B does not claim to prevent that case.

The VFS guard covers the generic VFS mutation API only. FAT32 metadata has no Unix-style owner model. Direct driver access, raw block writes, or privileged kernel code can modify storage outside this API.

The policy operation IDs for application, process, and general administrative filesystem operations are extension points only. They do not imply that those enforcement points are implemented.

## Verification evidence

- Identity foundation: 43/43 checks.
- Authentication and authorization sessions: 57/57 checks, including Administrator allow, Standard User denial, unassigned denial, revoked/disabled/deleted policy outcomes, live role changes, self-service target matching, direct identity-runtime mutation rejection, rejection of a second authentication runtime claiming the bound registry, credential invalidation, persistent and volatile bootstrap states, last-Administrator protections, and nonsecret audit diagnostics.
- Native Settings runtime: 1,447/1,447 checks, including permission flags, caller identity checks, authorized account creation and credential setup, Standard User rejection, and no-mutation checks.
- Settings Users model: 14/14; navigation: 8/8; interaction/layout: 13/13.
- Native Settings layout: 1,037/1,037 checks, including 1,000 resize geometries.
- Settings Center model: 89/89 checks.
- App Model identity: 25/25 checks.
- FAT32/VFS persistence: 44/44 checks against a disposable copy of the existing SECURITY3A raw image. The tests verify denial through generic open/write/create/delete/rename paths, alias denial, and successful internal security-store commit/readback. The source qualification image was not modified.
- AMD64 freestanding kernel build: `mingw32-make.exe -C kernel ARCH=amd64 -j2` succeeded and produced `kernel/build/amd64/bin/kernel.elf`.
- Hosted `cmd.exe /c build.bat`: succeeded and produced `guideXOSServer.exe`.
- No QEMU guest was started. The host-backed VFS test exercised the actual FAT/VFS sources; no QAC reservation was requested or changed.

SECURITY3B does not close the outstanding SECURITY3A truncated-slot guest case and does not change the SECURITY3A qualification outcome.

## Recommended SECURITY3C work

1. Establish per-process execution contexts that bind a process to a manager-issued session or explicit system-service identity.
2. Add a real application request boundary before attempting per-app permissions; keep normal application launching compatible while doing so.
3. Add process ownership only when the scheduler can persist and validate it, then enforce terminate-other-process operations at the process manager.
4. Route protected configuration and identity-store writes through capabilities that are unavailable to general kernel applications; later add persisted file ownership only with real VFS enforcement.
5. Add a QEMU scenario that exercises account authorization through native production call paths after the shared QAC window is available.
