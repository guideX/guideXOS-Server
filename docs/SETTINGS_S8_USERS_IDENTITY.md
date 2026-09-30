# Settings S8: Users and identity

## Finding

**guideXOS currently has no human-user identity model.** There is no guideXOS account database, stable user ID, login/authentication flow, per-user profile, authoritative desktop-session identity, or user authorization layer. The hosted server and the kernel can represent processes and applications, but those identities name software processes, not people. S8 does not introduce accounts or a speculative security contract.

The useful answer to “who is the current guideXOS user?” is therefore **not defined**. The hosted process runs under a Windows process token and its access to host files is subject to Windows; guideXOS does not read that token’s account name or promote it to a guideXOS identity. The hosted runtime may report “Hosted on Windows” as environment context.

## Identity boundaries

### Human identity, sessions, and profiles

- The kernel `Process` record has process ID, state, entry point, stack, and process name. It has no UID/GID or account owner.
- The hosted process table and App Model associate a process with an App Model ID and process metadata. This identifies an application.
- There is no login/session manager, authenticated sign-in state, account lifecycle, user switching, lock/sign-out operation, or authoritative current-human identity.
- `/users/default/apps` is a registry source convention (`UserApps`), not a home directory, account record, or profile owner.
- Desktop preferences are shared configuration. They are not a profile attached to a human identity.

The kernel shell previously printed canned `root`, UID/GID 0, one-user session accounting, `/home/root`, and owner/group values. Those strings were not backed by kernel identity or VFS ownership state. S8 removes those claims: identity and session commands report unsupported, `~` does not imply a home directory, and listings no longer invent an owner. This is presentation cleanup; it adds no UID, account, or privilege state.

### Process, App Model, and service identity

App Model IDs such as `gxos.builtin.settings`, process IDs, process names, and manifest capability strings identify or describe applications. They do not identify a person, prove a person signed in, or represent permissions granted by a person. Some application capability checks exist, but there is no user consent or per-user permission store.

The hosted system-service wrapper authorizes its Settings operations using the current simulated process ID/name plus the built-in Settings App Model ID and registry availability. This is a caller-application check, not human authentication. COM2 service peers are unauthenticated; the Settings page reports peer authentication as **Unavailable** and links to Developer → Services for transport diagnostics. It does not imply that built-in caller checks authenticate a remote service peer.

### Host Windows identity

Hosted system information reads runtime, computer name, architecture, processor, firmware, and installed-memory details. It does not read or display the Windows account name. Users labels the runtime separately from guideXOS identity and explicitly says “Windows user — Not guideXOS user.” Host file access remains subject to the Windows token/ACL of the running server process, outside guideXOS account management.

### File ownership and permissions

The kernel process and VFS interfaces do not expose an owner ID or enforce per-user file ownership. Hosted filesystem listing likewise exposes names, sizes, and directory state, not guideXOS owners. FAT32/exFAT metadata is adapted to generic permission bits, not account ownership. ext4 and NTFS formats contain ownership/security fields, but format fields alone do not mean the active VFS path exposes or enforces those identities; the current ext4 path does not implement that ownership contract. Settings therefore reports per-user file ownership and separate user permissions as **Not supported**.

## Settings ownership today

| Area | Current owner and scope |
| --- | --- |
| Wallpaper/background selection | Shared display-options configuration, with the existing `desktop.json` compatibility/fallback path. Imported background assets are in the shared `/user-data/backgrounds` store; there is no per-user asset/profile namespace. |
| Theme and desktop appearance | Shared display-options configuration, with the existing `desktop.json` compatibility/fallback path; not a user profile. |
| Enhanced focus indicator | Shared `desktop.accessibility.enhancedFocusIndicator` preference in `desktop.json`, consumed by the shared focus indicator. |
| Display arrangement and mode | Global compositor/display control plane. |
| Network | Kernel/global network owner. Settings is read-only where mutation is rejected by the current service contract. |
| Date and time zone | Shared display-options configuration (with the existing `desktop.json` compatibility/fallback path); the wall clock comes from the hosted runtime. |
| Apps and application capabilities | System-wide App Model registry and its registered sources; no user-grant store. |
| Devices and storage | System/kernel inventory snapshots, not per-user inventories. |
| System information | Read-only host/runtime and guideXOS build information. |

S8 migrates none of these settings. A future profile implementation should first decide which preferences are user-scoped and which belong to the machine/session, then introduce a real owner and migration plan.

## Users page

The Users page is read-only and has three bounded views:

- **Identity and account model** reports account model **Not supported**, current identity **Not defined**, authentication **Not supported**, separate profiles **Not supported**, shared desktop preferences, hosted runtime context, and links to session and security details.
- **Session and runtime** reports current guideXOS identity **Not defined**, session manager/sign-in/session actions **Not supported**, distinguishes App Model IDs from people, shows hosted runtime context, and labels the Windows user as not a guideXOS user.
- **Security capabilities** reports human authentication, separate user permissions, per-user ownership, and user-granted application access as **Not supported**. COM2 peer authentication is **Unavailable**, with a link to Developer → Services.

The presentation model in `settings_users_model.h` is deliberately only a UI model, not an OS identity contract. It bounds runtime text and distinguishes **Not defined** (there is no authoritative identity value), **Not supported** (the feature is not implemented), and **Unavailable** (a state/provider cannot currently be obtained).

There are no writable Users actions. S8 intentionally adds no add/remove account, display name, avatar, password/PIN, lock, sign-out, user-switch, or permission-toggle control. Those controls would imply systems that do not exist.

## Navigation and search

The global Settings search index remains bounded and deterministic. S8 replaces the misleading “User accounts” result with truthful destinations for Users and identity, current session, authentication/passwords, profiles, and permissions. Account/identity/profile searches lead to the overview; session searches open session details; password/permission searches open the security explanation.

The supported routes are `settings://users`, `settings://users/session`, and `settings://users/security`. Unsupported login/account-management routes are rejected. The service diagnostics route remains in Developer rather than being presented as a Users page.

All twelve top-level route names resolve: System, Display, Network & Internet, Personalization, Devices, Storage, Apps, Users, Date & Time, Accessibility, Developer, and About. Users joins the existing category renderer with substantive status information and detail navigation; no top-level category uses the generic placeholder renderer. The S8 model tests cover all twelve routes and all Users detail routes.

## Validation record

The S8 model suite passes **11/11 identity**, **8/8 navigation**, and **13/13 interaction/layout** checks. It covers no-account state, hosted-runtime bounds, unavailable versus unsupported labels, the Users routes and search terms, all twelve top-level routes, keyboard-focus navigation helpers, and 640×480 window-dimension/scroll calculations.

The full hosted `build.bat` build passes. It emits existing compiler warnings in unrelated code. The kernel `ARCH=amd64` build also passes. Existing Settings regression suites pass: Center **89/89**, S6 **42/42**, S7 Accessibility **21/21**, Developer **23/23**, navigation/search **14/14**, Devices **21/21**, Storage **24/24**, system-service bridge **78/78**, network contract **45/45**, network transaction **21/21**, and block-device generation **5/5**. Display control-plane, display persistence, synthetic-layout, and user-background regression smokes pass. The Startup App Model hosted smoke passes registration, explicit launch, no-unexpected-startup-window, and persistence checks for Notepad, Calculator, Display Options, Settings, and Control Panel.

The inherited Phase 5B closeout remains red at its existing `appModelV1StatusReady=true` assertion. The live diagnostic state documented in S7 remains `not-ready`, with 94 preview records, 12 unresolved, and 12 high-risk cases; its stale later fixture expects 18 built-ins while the live registry has 19. S8 does not change these assertions or preview findings.

The native hosted UI could not be opened in the available execution surface: no native app windows or app-launch API were exposed. Therefore the full visual/keyboard traversal, live 640×480 visit to all twelve categories, and enhanced-focus off/on runtime check remain **unverified**. The model and build checks pass, but they are not represented as visual runtime proof. S8 adds no kernel identity state, so QEMU and physical hardware were not used.

Validation build/test outputs are kept outside the source changes. No generated artifacts are staged or committed.

QEMU is not needed for S8: no kernel-owned account or session state was introduced. Physical hardware testing is not required.

## Future identity work

A future multi-user security phase should define an authoritative kernel/session owner and stable identity semantics before adding UI. It should then specify authentication, authorization, profile and home-directory ownership, filesystem enforcement, process ownership, capability grants, service-peer authentication, and the machine-versus-user scope of preferences as one coherent design. Until that work exists, “user” in UI or shell text must not stand in for a guideXOS account.
