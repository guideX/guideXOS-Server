# Server v0.3 — S22R17 Public Preview Security Gate

**Date:** 2026-11-08

**Outcome A — `PUBLIC_PREVIEW_BOUNDARY_FEASIBLE`**

**Readiness:** Public distribution remains blocked pending S22R18 enforcement/qualification and owner decisions.

## 1. Owner decision and approval limits

The release owner explicitly selected **Model B — Public Technical Preview** as the intended Server v0.3 release target. This is a scope decision recorded from the S22R17 instruction. It does not approve a security design, accept residual security risk, approve the complete preview contract, authorize recipients, authorize distribution, or change any existing release gate.

| Decision | S22R17 disposition |
|---|---|
| Intended distribution | Model B — Public Technical Preview |
| Decision source | Explicit release-owner instruction supplied for S22R17 |
| Intended audience/engineering target | Public evaluators, initially limited to the supported disposable VM profile in §3; engineering target is an immutable, verified Server v0.3 image with only the qualified first-party PacMan Native ELF package |
| Security risk acceptance | **PENDING** |
| Supported VM profile approval | **PENDING** |
| Release contract approval | **PENDING** |
| Preview recipients and distribution authorization | **NOT GRANTED** |
| Other S22R16 owner decisions | Remain pending unless listed as accepted evidence in §11 |

The S22R16 draft contract remains a draft. The limited VM boundary below is an architecture-feasibility recommendation, not an approved product contract or implementation decision.

## 2. Repository provenance and preservation

| Check | Result |
|---|---|
| Repository | `D:\dev\guideXOSServer` |
| Starting branch / HEAD | `main` / `fca51bf2dffdc5e2bdb8d4ee5a2ee80c8b234332`, exact expected S22R16 HEAD |
| Starting parent | `9486b696b7cfd1f02e522e0b7d83385b8c5a9701` |
| Ancestry | Expected S22R16 commit is the starting HEAD and therefore an ancestor |
| Starting tracked/staged worktree | Clean |
| Starting untracked file | `assets/Backgrounds/troncity.png`; SHA-256 `444ac64211907d80f0266e2283960cefaa932803d0aee7e0ccfd414f6fae2fda` |
| Phase ownership marker | No Main Integration `.phase` file was present in the checkout |
| Concurrent checkouts | Separate worktrees exist for `codex/gfx1-presentation`, `codex/server-vfs1`, and `INPUT_RELIABILITY`; not inspected for changes or modified |
| Candidate ISO | `2fe2f5eda87c9a80164715a156af5de12c7cf3891d29a961c40012c9d032241e` |
| Candidate kernel | `b5e029d23f9c21b5ff0838d9388322bf95b211bf3ef98145ddd344170dfe95dd` |

The receipt-selected ISO is `out/release-profile/6ad9bb77-12ce-4b4d-8ed0-56ff3944ee5d/artifacts/guideXOS-Server-v0.3.0-amd64-pacman-only-candidate.iso` (build ID `6ad9bb77-12ce-4b4d-8ed0-56ff3944ee5d`, profile `ServerV03PacManOnly`, recorded source commit `5b62216efe78158fb12707c126ec38582faecf59`). Its receipt says the worktree was not clean at packaging start. The signature proposal in this report would authenticate those exact bytes; it would not retroactively establish clean-source provenance or turn the S22R14R2 content-equivalence evidence into byte-identical ISO reproduction. Whether that existing provenance is acceptable for a public preview remains an owner decision. No rebuild is proposed in S22R17.

The candidate ISO, kernel, `dist`, publication state, release-readiness markers, and unrelated artifacts were not modified. No reset, clean, stash, rebase, merge, cherry-pick, branch switch, worktree creation, push, ISO rebuild, or QEMU run occurred. The only test run was the existing hosted ABI layout fixture described in §8. The pre-existing untracked background image remains untouched.

The starting branch was one commit ahead of `origin/main`. This phase adds only release documentation and a reference update to the still-draft S22R16 contract; its final commit and worktree state are reported with the S22R17 implementation plan.

## 3. Finding: a bounded public VM preview is feasible

The smallest defensible boundary found is **a public, disposable VM preview of one authenticated, immutable Server v0.3 image**:

1. The operator verifies a detached, publisher-authenticated release digest for the exact ISO before use. The checksum must come from a trusted release channel and must not be sourced solely from the image or writable guest filesystem.
2. The VM boots that exact ISO as read-only media. The VM has no writable persistent block device, no writable `/Apps` mount, no network adapter, no USB/storage passthrough, no shared folders, and no host clipboard/file integration.
3. The approved image contains only the already qualified first-party PacMan Native ELF application. No app installer, package update, external package import, or third-party Native ELF execution is supported.
4. S22R18 adds a release-profile runtime guard that refuses Native ELF discovery/launch unless `/Apps` and every resolved package path (manifest, executable, and resources) use the intended read-only VFS mount, with no writable nested mount shadowing it. A writable or missing package mount fails closed; it must not silently fall back to unrestricted loading.
5. VM snapshots/recreation provide recovery. Guest filesystem persistence is not part of this profile.

This is a real, layered boundary: authenticated artifact identity at the operator boundary, read-only media and an enforced read-only package mount inside the guest, and host isolation supplied by the selected hypervisor configuration. It is not provided by the PacMan-only package list. The current runtime does not yet enforce the `/Apps` read-only precondition, the publisher has no audited detached-signature workflow in this checkout, and the accepted guest evidence used a writable FAT32/ATA root. Therefore the current candidate is **not qualified for this proposed profile**.

The supported threat model trusts the user's host OS, hypervisor, firmware, release-signing key, and the user to verify and retain the exact read-only ISO. It does not claim protection against a host administrator/malware that can replace the VM media or hypervisor configuration. If the owner requires resistance to those attacks, the project needs a trusted boot chain (for example UEFI Secure Boot with controlled keys) and more extensive image/key lifecycle work. A VM does not repair vulnerabilities inside guideXOS: a Native ELF app still has kernel privilege and can corrupt the guest kernel or guest memory. VM isolation only limits the guest's direct access to the host, subject to hypervisor/device-model vulnerabilities and host-integration settings.

This route is intentionally limited to the current trusted first-party PacMan package. It does not support untrusted or third-party Native ELF programs. If those must run, the project needs the isolation architecture in §7.3.

## 4. Threat model and reachability

“Reachable” distinguishes the current implementation and observed release workflows from the recommended S22R18 profile. A hypothetical ingress route is not treated as proven merely because the kernel has a network or filesystem component.

| Actor / path | Current reachability and evidence | Consequence in current architecture | Proposed-profile handling |
|---|---|---|---|
| Ordinary guest user | The desktop exposes file operations. VFS/FAT implements create, write, copy, move, and delete. Whether a user can target `/Apps` depends on which root volume is mounted; the accepted guest qualification observed `/` on FAT32/ATA and PacMan at `/Apps`. There are no per-user file ACLs in the audited package-loading path. | Files placed under `/Apps/<dir>` can be discovered at the next scan/boot if they satisfy the loader's self-declared fields and ELF checks. | No writable persistent disk or package mount. Runtime refuses Native ELF launch when `/Apps` is writable. No package install/import workflow is supported. |
| Native ELF application | Demonstrated by PacMan launching through `kernel/core/native_elf_baremetal.cpp` and `kernel/arch/amd64/native_elf_call.asm`. The app stack is separate; the address space, privilege level, page tables, and kernel mappings are shared. | Code can bypass callbacks and directly corrupt kernel state, VFS, devices, other applications, or filesystem memory. Callback checks do not constrain kernel-mode code. | Only the exact first-party PacMan from the authenticated immutable image is permitted. A bug in that trusted app may still corrupt the guest. The hypervisor is the host boundary. |
| Malicious or incompatible package copied to writable storage | No dedicated package installer was found, but the writable VFS/file-operation paths and externally modifiable ESP/persistent storage can introduce files. S22R8 documents offline ESP additions being scanned on the next boot. Runtime discovery accepts up to eight package directories. | An accepted package executes at kernel privilege. An incompatible package can misuse a mismatched host table; a malicious one can corrupt the kernel. No malicious ELF was executed for this audit. | Unsupported: there is no writable persistent disk or package import path. The read-only mount guard blocks discovery/launch if configuration violates this rule. |
| User modifies ESP externally | Reachable on ordinary bare-metal boot: the ESP can be mounted writable at `/`; externally added package directories are discovered on a later boot. S22R8 distinguishes this from the read-only ISO path used when no writable persistent volume is available. | Release-time package selection does not constrain later storage contents. | Unsupported deployment. Use the verified ISO as read-only VM media. A media mismatch fails the operator's detached digest verification. |
| Person replaces/modifies the boot image | No guideXOS-owned secure-boot policy or kernel/ELF signature-verification path was located in audited sources. UEFI firmware configuration is outside the checkout. An actor able to replace the host-side VM image can substitute a kernel and bypass guest checks. | The current loader cannot authenticate the kernel or boot path. A replacement kernel can ignore all app controls. | Host/hypervisor administrator compromise is outside this profile. A trusted detached signature on the ISO digest plus read-only media protects the intended operator workflow. Resistance to host-level image replacement requires a Secure Boot/verified-boot project and owner-approved key management. |
| Remote package source | No app installer or Native ELF download path was found. Navigator/network behavior was not qualified on the accepted candidate; no supported workflow that installs remote content into `/Apps` was identified. | A future download/import feature would become a new package-ingress route. Kernel-mode parsing/network code itself remains part of the guest attack surface. | No VM network adapter; remote browsing and installation are unsupported. Validate the no-NIC profile in QAC before making the claim. |
| Hypervisor / host integration | The prior QAC profile proves one OVMF/QEMU setup boots the exact candidate, but it is not evidence of the proposed no-writable-disk/no-integration configuration. | A vulnerable hypervisor/device model or enabled shared device could expose the host to guest attacks. | Use a supported disposable VM with no shared folders, clipboard/file exchange, network adapter, or passthrough. Keep the hypervisor patched. Residual hypervisor escape risk remains. |

### Consequences to protect

- **Kernel integrity:** Current Native ELF code has kernel privilege; no app-level boundary exists. The proposed profile limits executable content to the verified first-party image and stops loading if `/Apps` is writable. A PacMan defect remains able to corrupt the guest kernel.
- **Guest filesystem/data:** The read-only image protects the shipped runtime and package files from ordinary guest writes. The preview has no persistent guest-data promise; do not store user data in it. If persistent user data becomes a requirement, add a separate writable data mount and prove that it cannot alias or replace `/Apps` before supporting it.
- **Host exposure:** No direct guideXOS host access is claimed. Host protection comes from the hypervisor and from disabling host integration. Guest vulnerabilities are still relevant; the VM is not proof of kernel security or an absolute escape barrier.

## 5. Native ELF loader and ABI enforcement audit

Production v0.3 execution is in `kernel/core/native_elf_baremetal.cpp`; the root-level hosted resolver/executor is a different development path and is not guest-runtime evidence.

| Contract | Current enforcement | Missing/bypassable step |
|---|---|---|
| Package discovery | `discover()` enumerates directories under `/Apps`, skips dot names, and parses up to eight packages. | No release-only allowlist or mount-read-only requirement. Runtime accepts packages added after build. |
| Manifest structure and identity | `parse_package()` extracts `kind`, `runtime`, `id`, `displayName`, `architecture`, `path`, `entryPoint`, and `abi` using bounded substring searches. It requires `NativeElf`, `native-elf`, `amd64`, `gx_main`, and the ABI string. | It does not structurally parse JSON, authenticate `id`/name, bind directory identity, bind manifest to ELF bytes, validate the top-level architecture declaration, or check a signature. Duplicate/nested text can confuse substring extraction. |
| Executable content integrity | ELF header, program-header, segment, image-span, and architecture checks exist. | No cryptographic package or executable digest is checked at runtime. Build-time hashes/receipt checks do not authenticate later storage. File reads are separate operations, without an immutable snapshot or lock spanning manifest/header/program-header/segment reads; a mutable source creates a potential TOCTOU surface. |
| Product-version requirement | Release build/receipt tools check selected package version declarations. | Runtime ignores `minGuideXOSVersion`. Kernel `version.h` reports `0.1.0` while the release product target is `0.3.0`; a canonical runtime product version is needed before adding a meaningful comparison. |
| CPU/ELF architecture | Bare-metal loader requires ELF64, little endian, `ET_EXEC`, AMD64 machine ID 62, bounded program-header count, load segments, and a bounded image span. Manifest entry requires `amd64`. | This proves only the implemented ELF subset, not product compatibility or trust. The code records an executable-range envelope but checks `e_entry` against the overall loaded-image range, not membership in a `PF_X` segment. |
| Entry point | Manifest string must be `gx_main`; loader calls the ELF header's `e_entry`. | No ELF symbol lookup proves that `e_entry` is the symbol named `gx_main`; entry permission is not checked per executable segment. |
| ABI identity/layout | Manifest and SDK identify `guidexos-c-abi-v1`; host API version is `0`; v0.3 host table is 256 bytes. | No required table size, ABI layout ID, service/callback negotiation, or pre-entry rejection exists. The incompatible v0.5 table is 456 bytes but shares the same ABI name and version. A compatible-looking v0.5 package can pass current manifest checks because minimum version is ignored, then read incompatible callback offsets or beyond the v0.3 table. |
| Callback availability and permissions | Host table is zeroed and supported callbacks are installed. Unsupported callbacks remain null. The `audio.output` permission is parsed and enforced by `host_play_pcm`. | Required callback sets are not checked before entry. The audio permission gate is a convention enforced only when the callback is used; native code can bypass it. There is no general application execution-permission boundary. |
| VFS access and package modification | App callbacks expose package-relative resource reads; these are path-limited. General VFS/FAT/file-manager writes exist in kernel code. | Kernel privilege means a Native ELF can call or modify kernel services directly. Package-relative callback restrictions are not confinement. Writable `/Apps` is not blocked by runtime policy. |
| Execution privilege | ELF code is called directly on a dedicated 128 KiB app stack using the Microsoft x64 calling convention. | No CPL3 transition, per-app address space, separate page tables, kernel/user mapping split, or syscall mediation exists on the AMD64 Native ELF path. The stack is not a sandbox. |

### ABI compatibility conclusion

The same `guidexos-c-abi-v1` string names incompatible v0.3 and v0.5 host tables. v0.3 has `GX_API_VERSION == 0` and 256 bytes; v0.5 is 456 bytes with different callback offsets (for example, offset 240 is `play_pcm` in v0.3 and a build callback in v0.5). A manifest label, `size` written by the host, or packaging profile does not negotiate a minimum table size or the callback slots an app will use. The bare-metal loader does not compare `minGuideXOSVersion`, and it does not prove that the ELF corresponds to its manifest. Thus malformed/incompatible content can reach the indirect entry call before compatibility rejection; no deliberately incompatible binary was executed.

For the immutable preview route, the entire ISO becomes the authenticated content unit. S22R18 must still fail closed unless the runtime package mount is read-only and the release image has the one approved package. This restricted contract avoids claiming general ABI compatibility. Any writable-package or cross-version promise requires package-level authenticated identity and explicit ABI negotiation; any untrusted third-party promise additionally requires process isolation.

## 6. Minimum enforceable Model B boundary

| Element | Proposed boundary for the limited public VM preview |
|---|---|
| Deployment | Disposable VM only, using the exact authenticated ISO as read-only boot/runtime media; no writable persistent disk, external boot media, network, shared folders, clipboard/file exchange, USB/storage passthrough, or production host data. |
| Permitted applications | Exact Server v0.3 image composition with first-party PacMan only. No third-party app support, package installer, package import, or cross-version Native ELF compatibility promise. |
| Executable verification | Detached publisher signature over the exact ISO digest, verified before boot from a trusted release key/channel. The guest only starts Native ELF after `/Apps` is confirmed read-only. Whole-image verification binds bootloader, kernel, ramdisk, manifest, ELF, and resources. |
| Trusted verification root | Publisher public key/detached signature outside the ISO and outside guest-writable storage; trusted host OS/hypervisor and firmware configuration; read-only ISO attachment. No current signing workflow or secure boot is claimed. |
| Package updates | New publisher-authenticated ISO image only; verify digest/signature, replace media, recreate or reset disposable VM. No in-place app update or install. |
| Storage modification policy | `/Apps` and its resolved manifest/executable/resource paths must use a read-only VFS mount, with no writable nested mount shadowing the package tree. Runtime rejects package discovery and launch on writable/missing paths. No persistent writable volume in the initial profile. |
| Failure behavior | Refuse Native ELF discovery/launch; report a clear diagnostic; keep the desktop available if safe; do not retry from another writable path and do not execute an unchecked fallback. |
| Recovery | Discard the VM, restore a known-good snapshot, or recreate from the authenticated ISO. No guest-state recovery or user-data durability promise. |
| Unsupported features | Physical/production deployment, writable application storage, user-installed apps, third-party Native ELF, persistent guest data, network browsing/download, host-shared resources, arbitrary firmware/image modification, and hostile-hypervisor resistance. |
| Residual privileges/risks | PacMan remains kernel-privileged and can corrupt the guest. Hypervisor or device-model vulnerabilities may expose the host. The model does not stop a host administrator from substituting a different VM image; it requires the supported operator workflow to verify and retain the signed image. |

**Integrity, compatibility, and isolation remain separate:** integrity is the authenticated ISO content plus read-only package mount; compatibility is limited to the exact first-party binary qualified in that image; isolation from the host is supplied by the hypervisor. guideXOS does not isolate Native ELF from its kernel. None of these properties implies either of the other two.

## 7. Security approach comparison

| Approach | What it can establish | Assessment for Model B |
|---|---|---|
| **1. Restricted first-party Native ELF** | Authenticated image/package identity, version/architecture/ABI preflight, fail-closed loading, and trusted update rules. A signature must cover the manifest and executable together and be checked against a key protected by the boot trust chain. Load and execute the same verified byte snapshot. | Defensible for trusted first-party code, not a sandbox. The current package list alone is not security. Supporting writable `/Apps` robustly would require runtime signature verification, trusted key lifecycle, TOCTOU-resistant snapshots, and a protected kernel/boot root. PacMan can still damage the guest. This is a fallback if writable packages or non-VM deployment are required. |
| **2. VM-only immutable preview** | A read-only verified image prevents ordinary guest writes to packaged apps; a VM configuration limits guest access to host resources. No writable storage, network, or app-import path makes external package ingress unsupported. | **Smallest feasible public boundary** when the contract is limited to the exact image, verified read-only media, no package storage, and disposable VMs. It requires a runtime read-only mount guard and new QAC qualification. It does not secure the guest kernel against its trusted Native ELF app and does not defend a compromised host/hypervisor. |
| **3. Isolated Native ELF applications** | User-mode apps, separate address spaces, kernel-mediated calls, capabilities, memory permissions, per-process lifecycle/cleanup, and versioned ABI negotiation. | Required for untrusted or third-party Native ELF. This is a substantial kernel/process architecture program, not an S22R17 or single-milestone change. Do not imply it exists. |

Approach 2 is the selected technical recommendation, not an owner-approved security model. If the owner rejects its no-writable-storage/VM assumptions, revert to Approach 1 with authenticated mutable packages and verified boot, or Approach 3 for untrusted applications. Do not widen the contract through warnings alone.

## 8. Feasibility evidence and validation

| Evidence/check | Result and limit |
|---|---|
| Repository and candidate | Starting commit matched the requested S22R16 HEAD. Read-only hash checks reconfirmed ISO `2fe2f5ed…32241e`, kernel `b5e029d2…fe95dd`, and preserved background image `444ac642…e2fda`. |
| S22R6–S22R8 records | Reviewed the ABI boundary, package ship-set, v0.5 incompatibility, first-party package caveats, and the distinction between read-only ISO boot and writable ESP/persistent storage. |
| Production source trace | Inspected `kernel/core/native_elf_baremetal.cpp`, `kernel/arch/amd64/native_elf_call.asm`, `kernel/core/vfs.cpp`, `kernel/core/ramdisk.cpp`, `kernel/core/main.cpp`, `kernel/core/desktop.cpp`, and `sdk/include/guidexos/abi.h`. Source confirms an existing read-only ramdisk mode and a VFS `readOnly` flag; source does not enforce that `/Apps` must be read-only before ELF execution. |
| Hosted safe test | Ran the pre-existing `build/s22r6_native_abi_layout_test.exe`: **PASS** (`Native ABI layout test PASS`). It validates layout/offset assumptions only; it does not test production manifest parsing or execution rejection. |
| Safe malformed/incompatible fixture | No production runtime fixture seam exists. No malformed or incompatible Native ELF was run. Creating a hosted-only duplicate of the production parser would not prove the guest loader and was not added. |
| Guest qualification | No new QEMU/QAC run. Existing accepted-candidate guest evidence used a writable FAT32/ATA root and therefore does not qualify the proposed no-writable-disk VM profile. S22R18 must qualify that profile through the established QAC reservation workflow. |
| Artifacts | No release binary, ISO, kernel, `dist`, readiness marker, or publication state changed. No QEMU run or rebuild occurred. |

The R15 reproducibility contract remains content equivalence, not byte-identical ISO reproduction. The exact accepted candidate remains the only candidate with the cited guest qualification; no guest evidence transfers to another ISO identity or to the new no-writable-disk configuration.

## 9. Remaining release-owner decisions

All decisions below remain pending; the Model B target selection does not resolve them.

| Topic | Evidence / unresolved decision |
|---|---|
| Security risk | Owner must accept the limited VM threat model and residual kernel-privileged PacMan risk, or choose further security architecture. |
| Supported deployment | Decide whether VM-only, no writable storage/no host integration, is acceptable for a public technical preview. Physical hardware and writable package storage are outside the proposed contract. |
| `/Documents` | The visible Documents workflow cannot create/use the expected folder on the accepted candidate. No data loss was observed, but users cannot rely on the browse/save path. Decide whether to exclude it with clear UI/docs or require a qualified fix. |
| Recent history | FAT rejects `/.startmenu_recent`; in-session recents work, while persisted recents may be empty/stale after restart. Decide whether the documented limitation is acceptable. |
| Legacy Display | Display configuration is unavailable, while a visible route can imply support. A bounded UI clarity correction is recommended; do not build a backend without scope approval. |
| COM2 | Preserve the accepted read-only `GetStorageSnapshot` evidence. Decide whether the external host endpoint is in the supported contract; do not advertise writes or unsupported storage operations. |
| Reproducibility | Keep the accepted content-equivalence contract and explicitly avoid a byte-identical ISO claim. Decide whether stronger reproducibility is required. |
| ABI / Native ELF | Accept only the exact qualified PacMan image in the limited model, or require the package-signing/ABI/isolation work described above. No cross-version compatibility promise is supported. |
| Contract, recipients, publication | Approve a revised public VM contract, name any recipients/operating controls as needed, and authorize distribution explicitly. Publication remains unauthorized. |

## 10. Model B readiness matrix

The matrix is for the intended Model B target. `PASS` on the owner-selection row records only that Model B was selected. No security gate is passed by the existing PacMan-only ISO boot.

| Gate / status | Threat or requirement | Existing evidence | Currently enforced protection | Missing protection | Proposed implementation | Required validation | Residual risk | Owner decision needed |
|---|---|---|---|---|---|---|---|---|
| **Model B target — PASS** | Record intended public audience and engineering target. | Explicit S22R17 owner instruction. | Decision is recorded here. | None for target selection; all release approvals remain separate. | Retain this decision reference in the release docs. | Review S22R17 record. | No risk acceptance or publication authority follows. | None for the selection; approval of risk/contract/distribution remains pending. |
| **Publisher artifact identity — ENGINEERING_REQUIRED** | Prevent substitution of the official ISO before use and state its provenance accurately. | Accepted ISO and component hashes are recorded; release receipt validates build identity but says packaging started with a dirty worktree. | Build-time SHA-256 and content inventory. | No audited detached signature over the public release digest; no in-guest verification and no protected boot chain. | Publish a detached publisher signature over the exact ISO digest; operator verifies it before read-only VM attachment. Preserve the receipt's dirty-worktree provenance or require a separately rebuilt/qualified candidate if the owner requires clean-source evidence. | Verify good signature passes, altered ISO/digest fails, and workflow uses an out-of-image public key; review exact source/build provenance. | Compromised signing key or trusted host can still substitute an image; signing does not make the build reproducible. | Approve publisher key custody, distribution channel, and whether recorded candidate provenance is acceptable. |
| **VM host boundary — ENGINEERING_REQUIRED** | Limit guest faults from reaching host data/devices. | Exact candidate booted under one QEMU/OVMF profile. | Hypervisor supplies a VM boundary when correctly configured. | Current evidence does not show the proposed no-network/no-shared-resource/no-writable-disk profile. | Publish a disposable VM configuration with no network, writable disk, shared folders, clipboard/file exchange, or passthrough. | QAC verify exact device set, ownership, read-only media, and guest boot; inspect config and direct QEMU arguments. | Hypervisor/device-model vulnerabilities remain possible. | Approve VM-only supported environment and its host precautions. |
| **`/Apps` immutability — ENGINEERING_REQUIRED** | Stop guest users from adding/replacing Native ELF files. | Runtime scans `/Apps`; VFS supports writes; read-only boot ramdisk exists when no writable persistent volume is mounted. | VFS blocks writes on mounts marked read-only; no current ELF loader check requires this state. | Current accepted guest setup had writable FAT32/ATA root; package discovery accepts later additions. | Add a release-profile guard requiring `/Apps` and each resolved package path to use the intended read-only mount, with no writable nested mount shadowing it; reject on writable/unknown mount. | Hosted policy tests and exact-profile QAC check the resolved mounts and that launch fails on writable/missing paths. | A kernel-mode app can bypass VFS; only the verified PacMan package is allowed. | Approve no writable package storage as a preview restriction. |
| **Application identity/content — ENGINEERING_REQUIRED** | Execute only approved first-party package bytes. | PacMan-only build profile and release composition/hash evidence. | Build-time selected package/hash checks. | Runtime identity is self-declared manifest data, not signed or digest-bound; later writable storage bypasses package selection. | Bind permitted app to the authenticated whole ISO and a read-only `/Apps`; package signature verification is required if that immutability contract changes. | Verify only release PacMan is present in authenticated ISO; safe modified package fixture is not discovered/executed. | Supply-chain compromise could place unsafe first-party code in an authenticated image. | Accept first-party-only scope and signing/build-key trust assumptions. |
| **Version/architecture/ABI compatibility — ENGINEERING_REQUIRED** | Reject unsupported server, CPU, ELF and ABI contracts before native entry. | v0.3/v0.5 table measurements and build-time version checks in S22R7/R8; safe hosted ABI layout test passes. | Basic ELF64/AMD64 and entry-manifest checks; release build checks selected packages. | Runtime ignores minimum Server version; shared ABI name/version maps incompatible 256/456-byte tables; no required size/callback negotiation; entry need not lie in a `PF_X` segment. | For restricted VM profile, exact-image package binding plus a release policy mapping the approved PacMan digest to the supported ABI. Add a canonical product version and versioned ABI descriptor before any broader package support. | Hosted policy/parser fixtures, freestanding loader tests, then QAC safe rejection before execution. | Trusted approved PacMan still shares kernel privilege; malformed/tampered images are outside contract only after artifact verification succeeds. | Approve exact-package-only compatibility scope or require general ABI support. |
| **Kernel/application isolation — ENGINEERING_REQUIRED** | Contain a Native ELF fault and protect host resources. | Source proves direct kernel-mode execution; the exact proposed VM configuration is unqualified. | No guideXOS app isolation; hypervisor isolation depends on VM configuration. | guideXOS has no CPL3 app execution, address-space separation, or mediated syscall boundary; no proof yet that host integrations are absent. | Do not accept untrusted Native ELF. Use only the authenticated first-party package in the restricted VM; qualify the hypervisor configuration. Schedule process isolation only if third-party apps are required. | QAC verify host integration is absent; separately qualify any later isolation architecture. | PacMan defects can corrupt the guest; hypervisor escape risk remains. | Explicitly accept this limitation for a preview or require Approach 3. |
| **Guest filesystem/data — ENGINEERING_REQUIRED** | Avoid user-data loss and package changes. | `/Documents` is unavailable; recent-history persistence fails; writable FAT exists in prior guest profile. | Read-only media protects shipped files in the proposed profile. | No persistent user-data support; actual no-disk guest profile not qualified. | Make guest state disposable; document no user-data durability; no writable data volume in first profile. | QAC verify no writable device and test read-only write failure safely. | Guest kernel bug may still corrupt its in-memory image/state; user changes are discarded. | Approve disposable/no-persistent-data limitation or require a separately mounted data volume. |
| **External ESP / boot-image modification — OWNER_DECISION_REQUIRED** | Define response to an actor with external media/host write access. | Normal writable ESP mode and external package additions are documented; no guideXOS signature chain found. | Read-only ISO plus operator digest verification in proposed VM setup. | Current runtime cannot detect a substituted kernel or altered firmware/VM config. | Treat host/hypervisor control as trusted boundary; if hostile external replacement is in scope, require UEFI Secure Boot/verified boot with owner-approved keys. | Verify signed image before attach; test firmware/VM policy and tamper rejection if secure boot is selected. | An attacker controlling the host/hypervisor can replace media/configuration. | Decide whether host-admin/firmware tampering is out of scope or fund verified boot. |
| **Remote package ingress — ENGINEERING_REQUIRED** | Prevent remote content becoming Native ELF packages. | No app installer/download workflow found; network browsing was not qualified. | None in general runtime. | Network-capable guests and future import paths are not blocked by package policy. | No VM NIC; no install/import feature in contract; future ingress requires signature enforcement. | QAC confirm NIC absent; test no package path is added. | A later kernel/network feature could create new ingress. | Approve no network/package import as a scope limit. |
| **Failure/recovery — ENGINEERING_REQUIRED** | Fail safely and restore known-good state. | Existing synchronous app cleanup; boot/read-only ramdisk paths. | No package-policy failure behavior exists. | No clear diagnostic for a writable `/Apps` refusal; no approved VM recovery procedure. | Fail closed to desktop/no Native ELF; recover by discarding VM, snapshot restore, or verified ISO recreation. | Hosted failure cases; QAC observe refusal and recovery. | Guest may still fault due trusted app defects. | Approve no persistence and recovery wording. |
| **Other functional contract — OWNER_DECISION_REQUIRED** | Ensure public expectations match supported behavior. | Existing candidate evidence and documented `/Documents`, recent, Display, COM2, reproducibility limits. | Current supported feature behavior only. | Owner has not accepted exclusions or selected the complete contract. | Carry the pending decisions in §9 into the revised contract. | Contract-to-evidence review against exact image/config. | Users may rely on unsupported behavior if it is not clearly excluded. | Decide each S22R16-D02 through D09 item. |
| **Publication authorization — BLOCKED** | Prevent unauthorized public release. | No publication authorization was supplied. | Existing release gates remain unchanged. | Risk acceptance, complete contract, signing workflow, recipients/operational evidence, and explicit distribution authorization. | Keep candidate undistributed until all gates and approvals close. | Final review against the exact signed image identity. | None can be accepted implicitly from Model B selection. | Explicit approval and distribution authorization required. |

## 11. Recommended S22R18 scope

Recommend **S22R18 — Immutable VM Preview Enforcement and Qualification**, subject to owner approval of the proposed model. First milestone: implement the `/Apps` read-only release-profile guard and its failure behavior. Then publish/verify a detached authenticated ISO digest and qualify the exact no-writable-storage VM configuration using the QAC reservation workflow. Preserve the current candidate as historical evidence; any changed kernel or ISO gets a new artifact identity and targeted requalification.

If the owner requires writable `/Apps`, physical bare-metal deployment, package installation, or an adversarial local host/firmware threat, stop this narrow route and choose a separate signed-package/verified-boot scope. If untrusted or third-party Native ELF is required, schedule the process-isolation architecture; do not extend the current kernel-privileged ABI by assertion.

## 12. Evidence index

- [S22R15 release readiness and owner decisions](RELEASE_S22R15_READINESS_AND_OWNER_DECISIONS.md)
- [S22R15 owner decision summary](RELEASE_S22R15_OWNER_DECISION_SUMMARY.md)
- [S22R16 owner decision closure](RELEASE_S22R16_OWNER_DECISION_CLOSURE.md)
- [S22R16 draft preview contract](RELEASE_S22R16_PREVIEW_RELEASE_CONTRACT.md)
- [S22R6 Developer Studio editor adapter trust boundary](RELEASE_S22R6_DEVELOPERSTUDIO_EDITOR_ADAPTER.md)
- [S22R7 Native ABI release boundary](RELEASE_S22R7_NATIVE_ABI_RELEASE_BOUNDARY.md)
- [S22R8 application ship-set qualification](RELEASE_S22R8_APPLICATION_SHIPSET_QUALIFICATION.md)
- [S22R14R2 build reproducibility evidence](RELEASE_S22R14R2_BUILD_REPRODUCIBILITY.md)
- [S22R14R1R5 guest interaction evidence](RELEASE_S22R14R1R5_GUEST_INTERACTION_AND_COM2_COVERAGE.md)
- [S22R14R1R6 COM2 and legacy Display evidence](RELEASE_S22R14R1R6_COM2_AND_LEGACY_COVERAGE.md)
