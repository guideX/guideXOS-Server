# SECURITY2 — Authentication, Sessions, and Administrator Foundation

## Scope

SECURITY2 adds native password credentials, authentication, interactive sessions, and human-account role metadata on top of the SECURITY1 identity registry. It preserves the existing unassigned desktop startup path. A user account, a stored credential, an authenticated session, an Administrator designation, and an authorized operation remain separate states.

This phase establishes authentication and role metadata. It does not implement system-wide authorization or isolate Standard User processes from Administrator processes.

## Accounts and roles

Human accounts have a stable `UserId`, an account name, a display name, an active/disabled status, and one of two roles: `StandardUser` or `Administrator`. `Administrator` is a human-account designation; it does not make the account the kernel identity or grant unrestricted kernel execution. The reserved SYSTEM identity remains nonhuman and cannot be assigned either human role.

Identity serialization version 2 stores the role. SECURITY1 version 1 records remain readable and migrate to `StandardUser`; they do not acquire credentials as part of migration. Account IDs are allocated independently of display names. Role, disable, deletion, and credential-revocation operations require an authenticated Administrator in the trusted runtime, except that a user can change or revoke their own credentials. Runtime guards preserve at least one active Administrator with credentials where the registry and credential store are readable.

## Credential format and cryptography

Passwords are not copied into identity descriptions or credential files. The credential service uses the PSA implementation in the pinned Mbed TLS 4.1.0 / TF-PSA-Crypto 1.1.0 profile:

- PBKDF2-HMAC-SHA256, 600,000 iterations for newly enrolled credentials.
- A unique 16-byte salt from the secure platform RNG.
- A 32-byte derived verifier.
- A stored record format version, algorithm identifier, iteration count, stable user ID, salt, and verifier. Supported stored costs are bounded to 600,000–1,200,000 iterations.
- A timing-safe verifier comparison. Unsupported algorithm IDs, versions, parameters, and malformed records fail closed.

Passwords must be 12–128 printable ASCII bytes. The service rejects out-of-range inputs before derivation. A PBKDF2 known-answer test is run before the platform derivation is accepted. Sensitive password, salt, verifier, and serialized-record scratch buffers are cleared where practical. Credential verifiers retained in the live service are necessary for authentication.

On AMD64, secure randomness comes from the ready virtio-rng device through `gxos_random_bytes`. The provider checks the secure-readiness state and propagates entropy failure; it has no timestamp, predictable PRNG, or deterministic-salt fallback. If the secure source is unavailable, credential enrollment and session-reference creation fail without disabling desktop boot.

The current format is intentionally not encrypted at rest. The KDF slows offline password guessing but does not protect verifier files from a storage reader; users should choose passwords accordingly. The CRC detects accidental corruption and incomplete records, not malicious modification.

## Persistence and recovery

Credential data is separate from the identity registry in two generation-numbered VFS slots, `/.gxos-credentials-0.dat` and `/.gxos-credentials-1.dat`. Each bounded store has a versioned header, sorted stable-ID records, and a CRC. A mutation writes the inactive slot, reads it back, and checks the complete bytes, generation, and CRC before reporting durable success. Invalid or unsupported slot data blocks authentication and mutations; the service does not silently fall back to an older password after a corrupt replacement/revocation slot is detected.

The native adapter currently uses a writable FAT32 root. Read-only, unavailable, corrupt, and unsupported states are reported separately. When the identity registry is volatile or the credential store cannot be written, in-memory enrollment/session work can be reported as volatile; durable success is not claimed. A credential is not allowed to survive independently of a volatile identity registry, which prevents a stale credential from binding to a later account ID. Account IDs are allocated from the monotonic human-ID range and are not reused by deletion.

If the account registry is empty, native Settings can create the first Administrator without a hardcoded password. If there is no active Administrator with a usable credential, the local recovery path may enroll an existing credential-less account; an existing credential must be proven before its account is promoted. A populated registry does not allow bootstrap to create an arbitrary new account. Bootstrap is local to the trusted built-in Settings runtime and is not exposed over a remote interface. Volatile or read-only storage does not prevent boot and is not reported as durable setup.

## Authentication and session authority

The trusted native `authentication::Runtime` resolves account names through the identity registry, then checks active account status and the stable-ID credential record before opening a session. Unknown names and accounts without credentials perform dummy PBKDF2 work and return the same invalid-credentials result. Per-name failures begin bounded exponential backoff after five failures; delay is capped at 3,000 PIT ticks, and the attempt table is bounded to 32 entries.

An active session is bound to the current human identity, current account eligibility, and current role. Session references are 128 random bits, collision-checked against the bounded in-memory session table. There is one current interactive human session; the table is capped at 16 records. Callers cannot select or restore a session by presenting a user ID or session reference. `signOutCurrent` ends the current session and the interactive identity provider then reports `InteractiveUnassigned`. Credential change/revocation and account disable/deletion invalidate the affected session.

The authentication service is kernel-owned and is not exposed through a syscall, COM2, or hosted Settings transport. Native Settings operations additionally require the AppManager-assigned `gxos.builtin.settings` runtime app ID. Sign-out invalidates the runtime session before Settings reports success. The random session reference is not an API for choosing a session.

## Settings and desktop compatibility

Native Settings → Users reads a validated value-only identity/authentication snapshot. It displays registry and credential-storage state, current display name, role, signed-in state, and whether credentials are durable. Its local controls support entering an account name and masked password, signing in, first-administrator bootstrap, and signing out. The password buffer is cleared on action, field blur, window close, and shutdown; it is not logged. The hosted Users model reports native identity/authentication/session facilities as unavailable when their native provider is absent.

Boot does not require sign-in. The desktop continues in the explicit unassigned compatibility context until a human account authenticates.

## Security boundaries and remaining work

Administrator/Standard User is role metadata only in this phase. It is not a privilege boundary. File ownership, per-user settings, application permissions, process isolation, syscall authorization, remote login, configurable login policy, and full graphical account management remain future work. Internal mutation methods are available to trusted kernel code; their current guards are not a general authorization framework for every privileged OS operation.

## Verification record

**Outcome B — `SECURITY2_AUTHENTICATION_SESSIONS_PARTIAL`.** Authentication, credential records, Administrator/Standard User metadata, sessions, and native Settings integration are implemented. The ordinary AMD64 kernel build and focused hosted regressions pass. The production PSA PBKDF2 path also passed a QEMU guest smoke test. Durable credential reload through the kernel's real VFS adapter was not qualified on a raw FAT32 guest disk, so SECURITY2 is reported partial rather than claiming fully verified durable provisioning.

Hosted verification:

- Authentication/session suite: 34/34, including failed identity persistence reporting and orphan-credential checks.
- Identity foundation and SECURITY1 migration suite: 43/43.
- Settings Users model: 14/14; Users navigation: 8/8; Users interaction/layout: 13/13.
- Native Settings runtime: 1,441/1,441, including provider role/display state, bootstrap, sign-in/sign-out, and caller checks.
- Native Settings layout: 1,037/1,037, including 1,000 resize-geometry transitions.
- AMD64 freestanding kernel build: passed with the pinned Mbed TLS 4.1.0 / TF-PSA-Crypto 1.1.0 profile verifier passing.

The QEMU guest used an isolated kernel built with `GXOS_SECURITY2_AUTH_SMOKE_TEST`, a host-backed VirtIO RNG device, and a scratch ESP. The test-only runtime used unavailable storage adapters, which forced the documented volatile mode; it used the actual bare-metal PSA provider to pass the PBKDF2-HMAC-SHA256 known-answer test, generate a secure temporary password, bootstrap an Administrator, authenticate, verify the Administrator session, sign out, and authenticate again. Final serial result: `production_psa_pbkdf2=pass volatile_enrollment=pass password_authentication=pass session_signout=pass result=PASS`. QAC reservation `ca3f48f00ac14829be98083381987a84` exited with code 0 and was released; final QAC status had zero QEMU processes, managed sessions, queued reservations, or blocked reservations. The default kernel build does not enable the smoke hook, and the guest desktop continued in the unassigned compatibility context.

The guest's directory-backed `fat:rw` ESP reported storage available, but first-administrator provisioning through that VFS adapter returned `ACCOUNT_UNAVAILABLE`, and no identity/credential slot files appeared in the scratch directory. That path did not provide evidence of a successful durable mutation or cold reload. Hosted persistence tests cover dual-slot serialization, reload, write/readback failures, and volatile/read-only results through test storage callbacks; they do not replace a raw FAT32 guest write-and-reboot qualification. This is the specific remaining SECURITY2 acceptance gap.

Recommended follow-up: qualify identity and credential write/readback plus cold-boot authentication on a private raw FAT32 guest image before upgrading the outcome to A. SECURITY3 should then enforce role-aware authorization at file, application, and process boundaries, with per-user data ownership and auditable session/credential revocation. Until those controls exist, role metadata is not an isolation or privilege boundary.
