# SECURITY3C — Application and Process Security

- **Project:** guideXOS Server
- **Branch:** `SECURITY_SETTINGS`
- **Outcome:** `SECURITY3C_APPLICATION_PROCESS_SECURITY_PARTIAL`
- **Starting baseline:** `969535555d061b7e12a8108969416787269c8215`

## Result

SECURITY3C adds a bounded application execution-policy contract and integrates it at the Native ELF pre-entry boundary. A Native ELF launch now retains a monotonic launch-instance record containing the canonical App Model ID and the validated current account/session provenance, or an explicit unassigned/unavailable owner state. The record says it has no independent process ID. It is attribution only and is not accepted as authorization.

The kernel process table remains a scheduler stub and does not back desktop applications. The Native ELF loader executes synchronously on the kernel stack with kernel privilege. There is consequently no sound native per-process owner or process-termination authorization target to enforce in this phase. No Native ELF isolation or containment is claimed.

Ordinary Native ELF launches continue to allow unknown or unsigned applications in the development profile and report verification as unavailable. Existing public-preview release-profile checks remain mandatory and unchanged. The release policy's final source, manifest, resource, and verified executable snapshot checks still run before entry.

## Runtime architecture and boundaries

### Native AMD64

- `kernel/core/process.cpp` contains a fixed array and a `schedule()` halt stub. Its `create_init_process()` API is not the scheduler for desktop applications and does not provide live process lookup, termination, PID reuse handling, or authenticated ownership.
- Desktop application dispatch resolves Native ELF packages through the native package table and hands the canonical App Model ID to `native_elf::launch` (`kernel/core/desktop.cpp`, `kernel/core/app_launch_target_resolver.cpp`). The launch call is synchronous and returns after the app exits.
- `kernel/core/native_elf_baremetal.cpp` owns a stack-local `Runtime` during launch. Its internal runtime counter and audio owner are not kernel PIDs. `LaunchAttributionV1.launchInstanceId` is a separate monotonic launch-event identifier; `hasIndependentProcessId` is always false.
- Native ELF code runs with kernel privilege. The host-call table narrows normal ABI behavior, but arbitrary native code can bypass those calls and access kernel state. This is not a sandbox or a security boundary against malicious kernel-privileged code.
- The hosted `ProcessTable` in root `process.cpp` starts real host threads and assigns hosted IDs. It has no authenticated account provider and its `terminate()` method does not perform SECURITY3B authorization. It is hosted functionality only. `NativeAppProcessTable` tracks hosted Native ELF runtime instances; it does not make the native kernel process stub authoritative.

### Package and registration paths

The native launch target is selected by canonical App Model ID after desktop resolution. The native Native ELF package table does not have AppRegistry's registration-generation field; its current launch handoff is synchronous, and release-profile source and generation checks revalidate the backing mount at discovery and pre-entry. Hosted AppRegistry registrations have their own generations for stale handler validation.

The root `PackageManager::InstallGXApp` path writes hosted `.gxapp` files through the hosted `FS` layer. No native installer, system-wide native package replacement API, or native system registration mutation boundary is wired to it. Hosted package installation is not represented as native authorization enforcement. Native app default-handler configuration also has no SECURITY3C administrator gate because there is no connected native system-wide app-registration mutation service.

## Launch ownership and session provenance

`authentication::Runtime::queryExecutionAuthority()` validates the current session through the existing authentication and identity runtimes, reads the current account role, and returns a monotonic in-memory session generation. It never accepts a user ID or session generation from the caller. A new sign-in receives a new generation; generation exhaustion prevents further session creation rather than reusing a generation. The generation is diagnostic provenance, not a bearer credential.

At the Native ELF launch boundary, the loader captures:

- the canonical application ID from the current validated package record;
- one monotonic launch-instance ID;
- the current validated user ID, role class, and session generation, when a session is active;
- explicit `InteractiveUnassigned` when the desktop has no signed-in account;
- `InvalidOrUnavailable` if authentication provenance cannot be queried or was invalidated;
- a reserved `TrustedKernelSystem` owner kind for system-created launches; current desktop package launches do not select that kind.

Display names, manifest publisher fields, and administrative capability declarations do not determine the owner. The saved role is historical launch attribution. A later role change does not rewrite it; authorization continues to use the authentication runtime's current role and session state. The launch record is not an input to `authorizeCurrent`, so a stale record cannot authorize a later administrative operation.

Sign-out, disable, deletion, credential revocation, and session invalidation prevent later authorization through the authentication runtime. Running Native ELF code is not automatically terminated on sign-out, and its launch record remains historical. No process-to-session revocation mechanism exists yet. Active launch records are in memory only and do not survive a cold boot.

## Process-management authorization

No process-management authorization is enforced in the native kernel because there is no active process manager with reliable target ownership. The reserved SECURITY3B `Process*` operation IDs remain policy-model extension points; this phase does not use them to simulate ownership. Hosted `ProcessTable::terminate()` is not an authenticated kernel boundary. Therefore SECURITY3C does not claim that Standard Users can terminate their own native process, that cross-user termination is blocked, or that Administrators can manage native process objects.

The `ApplicationLaunchRestricted` authorization operation supports a future policy rule that requires an Administrator. The evaluator invokes it only when a trusted policy input specifies that rule. No such local rule provider is active by default, so ordinary launches do not require Administrator status.

## Application execution-policy contract

`kernel/core/include/kernel/application_execution_policy.h` defines versioned, bounded application and publisher identities and a pure evaluator. Application identity contains a canonical App Model ID, optional package version, architecture, and optional executable digest. Publisher identity is a separate record containing display text, a public-key identity fingerprint, signature-verification status, and revocation status. Publisher display text such as `guideXOS` is never accepted as proof of signing authority.

The evaluator supports trusted applications, trusted publishers, explicit blocks, signed unknown publishers, unsigned applications, unknown identity, and verification-unavailable status. A trusted application digest requires a match reported by a trusted policy source. A trusted publisher requires verified signature status, a public-key identity, and a non-revoked state. A checksum by itself does not authenticate a publisher.

The deterministic precedence is:

1. Mandatory release-profile rejection.
2. Trusted-authority block or publisher revocation.
3. Administrator-controlled local rule (`Block`, `RequireAdministrator`, or `Allow`).
4. Verified trusted application or publisher classification.
5. Unknown/unsigned handling.

Providers report a higher-priority platform or trusted-catalog block through the separate `authoritativeBlock` input. An `ExplicitlyBlockedApplication` or `ExplicitlyBlockedPublisher` trust classification is evaluated after the Administrator-local rule, so the source of a block is not lost in one ambiguous flag.

The evaluator distinguishes `Allowed`, `Blocked`, `RequiresAdministrator`, `UnknownTrust`, `VerificationUnavailable`, `InvalidApplicationIdentity`, `ReleaseProfileRejected`, and `InvalidPolicyInput`. Unknown/unsigned handling defaults to compatible allow when no rule is configured. This phase does not install a trust registry, a local policy store, an online updater, publisher keys, or remote catalog data.

### Native launch integration

The loader constructs policy identity from the canonical package ID and architecture. Native ELF has no package signature verifier and no current package-version field, so publisher verification is `VerificationUnavailable`; the manifest publisher label is ignored. In the development profile the evaluator runs after image loading and immediately before entry, and preserves the existing allow behavior for unknown and unsigned code.

In the public-preview profile, the loader first preserves the existing PacMan-only immutable release policy. The loader checks the read-only release source and device generation, exact manifest/package identity, runtime resources, executable digest, and verified executable snapshot before policy evaluation. A failed mandatory check is recorded as `ReleaseProfileRejected`. The approved snapshot is the same byte sequence loaded for execution, so a substituted file cannot inherit that release-profile approval. The profile's pinned hashes are platform policy pins, not publisher signatures.

In the development profile, there is no executable signature/hash policy. The policy record reports verification unavailable and allows launch under the default development rule. Since no trust-based allow rule is active, no trust decision is made about the executable bytes in this profile.

## Settings and compatibility

Native Settings Users continues to show the current account, role, and session state through the SECURITY3B provider. It does not expose a Trusted Publishers page or policy toggles because no authoritative trust catalog or mutable policy provider exists. Hosted Settings continues to report native identity and authentication providers as unavailable.

No mandatory login was added. The unassigned desktop continues to launch ordinary applications. Unsigned development applications remain launchable. No process is assigned Administrator ownership because of its manifest or display metadata. The existing separately configured release-profile restrictions remain in force.

## Enforcement matrix

| Boundary | SECURITY3C status | Evidence and limit |
|---|---|---|
| Native Native ELF launch selection | **Enforced at launch boundary** | Desktop resolves the canonical App Model ID; policy runs before entry. The loader is still kernel-privileged. |
| Development unknown/unsigned launch | **Compatible allow, verification reported unavailable** | Evaluator is integrated into pre-entry; no default-deny or publisher-verification claim. |
| Public-preview PacMan restriction | **Existing mandatory native enforcement preserved** | Release source, generation, package, resources, and executable snapshot checks remain required before entry. |
| Native launch ownership | **Attribution metadata** | Runtime captures user ID, role class, session generation, canonical app ID, and launch ID. It is not a PID, capability, isolation boundary, or operation authorization input. |
| Native process ownership and termination | **Unsupported** | The kernel process table is a stub and is not the desktop scheduler. No cross-user process check is claimed. |
| Hosted process ownership and termination | **Hosted-only, unauthenticated** | Host threads have hosted IDs, but the hosted table has no kernel session authority and no SECURITY3C authorization. |
| Native system-wide package installation/removal | **Unsupported** | No native installer or authoritative system registration mutation boundary exists. |
| Hosted Package Manager | **Hosted-only** | Writes hosted `.gxapp` files; does not enforce native Administrator policy. |
| Trusted/blocked app and publisher registries | **Future policy hook** | Versioned identities and deterministic precedence exist; no trust or revocation catalog is active. |
| Native ELF sandboxing, memory isolation, or containment | **Not implemented** | Applications execute with kernel privilege and can bypass kernel API checks after entry. |

## Verification

- Authentication and session suite: **60/60** passed, including current account/role provenance, unassigned and invalidated states, distinct session generations, and Administrator/Standard User decisions for a restricted-launch rule.
- Application execution-policy and Native ELF release-policy suite: **41/41** checks passed, including precedence, development compatibility, unsigned handling, publisher/name separation, arbitrary checksum rejection as publisher proof, revocation, invalid and case-sensitive App Model IDs, release-profile restrictions, source replacement, and repeat launch checks.
- SECURITY1 identity foundation: **43/43** passed.
- Native Settings runtime: **1,447/1,447** passed.
- Native Settings layout: **1,037/1,037** passed.
- Settings Users model/navigation/layout: **14/14**, **8/8**, and **13/13** passed.
- Settings Center model: **89/89** passed.
- App Model identity: **25/25** passed, including registration-generation reuse checks.
- App Model file activation: **60/60**; folder activation: **26/26**; application actions: **55/55**; and default-handler/persistence checks: **54/54** passed.
- AMD64 freestanding kernel build: passed; `kernel/build/amd64/bin/kernel.elf` produced. Existing unrelated compiler warnings remain.
- Public-preview profile AMD64 build: passed into the isolated `kernel/build/security3c-preview/bin/kernel.elf` output directory; the normal build and frozen release artifacts were not replaced.
- Hosted `build.bat`: completed and refreshed `guideXOSServer.exe`; it does not compile the native kernel loader.
- QAC coordinator status was online with **1 managed session and 1 QEMU process**. No reservation was made and no guest was started; guest execution evidence is pending. The unrelated SECURITY3A truncated-slot case remains separate.

## Deferred work

SECURITY3D should add real kernel process objects tied to the scheduler, generation-safe process references, trusted creation-time ownership, session provenance, live ownership enumeration, cleanup, and authorization at process query/termination boundaries. Protected system processes need separate non-human authority and explicit non-terminable rules. This should be tested against actual scheduler paths, not mock ownership records.

SECURITY4 should establish user-mode execution with address-space and privilege separation, syscall/service capability checks, and a trusted signature-verification path. A later trust catalog can then use cryptographic publisher identities and independently authenticated allow/block/revocation policy. Until those kernel boundaries exist, Native ELF authorization remains launch gating and attribution, not containment.
