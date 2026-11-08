# TOOLS13R1P — Server Compatibility and Guest Admission Preflight

**Date:** 2026-11-08 (Pacific)
**Disposition:** **Outcome D — COMPATIBILITY_UNRESOLVED**

The local Server candidates contain a technically promising AMD64 Native ELF runtime for the accepted TOOLS12 artifact. Static inspection confirms that the ELF format, image base, file and mapped-size limits, entry point, and declared C ABI fit the Developer Studio Server branch. Related Native ELF and Developer Studio guest qualification records also exist.

No safe pairing is designated yet. Every local Server kernel version header inspected declares 0.1.0; the Developer Studio package declares a minimum guideXOS version of 0.5.0. The branch named v0.5_DEVELOPER_STUDIO has no matching Git release tag, and its header still says 0.1.0. The Server loader parses minGuideXOSVersion but does not compare it with a Server runtime version. No authoritative mapping between the kernel header and the manifest minimum was found. This leaves the version contract unresolved; no version field was changed or ignored.

QAC is online and idle, but the recorded coordination policy still reserves first QEMU priority for .NET Support and requires an explicit handoff. No reservation was requested and no QEMU guest was launched.

## Repository baseline

| Role | Repository / branch | HEAD | Worktree |
| --- | --- | --- | --- |
| Server integration checkout | D:/dev/guideXOS_DeveloperStudio_Tools — DEVELOPER_STUDIO_TOOLS | a13768dea9e5808baadb3c2908a8fca463505f16 | clean |
| Developer Studio source | D:/dev/guideXOS_DeveloperStudio_Tools_Source — DEVELOPER_STUDIO_TOOLS | 48f2da543517bd7238378c5a18769be148b10863 | clean |
| Strongest Server runtime candidate | D:/dev/guideXOSServerV0.5_DEVELOPER_STUDIO — v0.5_DEVELOPER_STUDIO | 801b4763aa09ddd6cab06bf726610063ca0389de | pre-existing changes present; preserved |

The source HEAD is the reported documentation commit 48f2da543517bd7238378c5a18769be148b10863, subject “TOOLS13R1: refresh guest preflight findings,” parent 8d761a67ab78daa9ac4cf95fae8e582f9c678d61. The commit is an ancestor of the current source branch. Comparing it with its parent shows only docs/TOOLS13R1_AMD64_GUEST_SMOKE.md changed. Comparing the TOOLS12 implementation commit de921d3d2ba49f630295443d2236dfa7da34fec0 with source HEAD shows only documentation changes: the TOOLS12 report was updated and the TOOLS13R1 preflight report was added.

All inspected worktrees and artifacts were left in place. The candidate Server checkout has pre-existing local changes; the version header, Native ELF runtime and SDK files, and qualification documents used below had no local modifications. No branch was switched, reset, stashed, cleaned, or altered.

## Version and compatibility contract

| Value | Source | Meaning established by source | Compatibility role |
| --- | --- | --- | --- |
| 0.1.0 | Server kernel/core/include/kernel/version.h, KERNEL_VERSION_STRING | Declared kernel version; KERNEL_NAME is guideXOS | No separate platform release manifest or runtime version API was found. Searches found no use of KERNEL_VERSION_STRING outside its definition; guest-visible product-version reporting is therefore unverified. |
| 0.1.0 | Developer Studio app/app.json, version | Application/package version | Identifies this application release. It is not the Server minimum. |
| 0.5.0 | Developer Studio app/app.json, minGuideXOSVersion | Declared minimum guideXOS runtime version | A package requirement, distinct from the application version and ABI. |
| 1 | app.json, schemaVersion | Application-manifest schema version | Server supports schema 1; this is not an OS version. |
| guidexos-c-abi-v1 | AMD64 manifest entry, abi | Native application C ABI name | Candidate Server SDK declares GX_ABI_NAME with the same value. Its separate numeric GX_API_VERSION is 0. |
| native-elf | AMD64 manifest entry, runtime | Execution mechanism | Describes the runtime kind, not a runtime release. |

The app manifest’s version and minimum are also generated for new projects in src/developer_studio_projects.cpp: version is emitted as 0.1.0 and minGuideXOSVersion as 0.5.0. These are deliberate separate fields in the current Developer Studio source.

### Manifest loading, validation, and rejection

In the Server integration checkout, app_manifest_loader.cpp parses minGuideXOSVersion into the manifest model. Searching the Server source for that field found the model declaration and parser assignment, but no comparator or call that compares it with KERNEL_VERSION_STRING or another runtime version.

app_manifest_validator.cpp rejects unsupported schema versions and validates required application identity, non-empty application version, kind, architecture, and executable entries. It does not reject a package because its minGuideXOSVersion exceeds the Server’s declared kernel version. The app registry selects a launch entry by architecture and kind. The Native ELF validator separately rejects unsupported or malformed ELF inputs. None of those paths implements the declared minimum-version comparison.

The actual minimum-version comparison is therefore **absent** in the checked-in package loader/validator path. This does not make the declared minimum optional for this qualification. We will not treat the absence of a gate as permission to launch a package on a runtime that has not been shown to satisfy it.

### Build and package metadata flow

Developer Studio’s build.ps1 reads app/app.json from the source checkout and copies it verbatim into the package’s Apps/DeveloperStudio/app.json. ServerRoot selects the Server SDK headers and, unless StagingRoot is supplied, the package destination. The build does not derive or reconcile the application minimum from a Server version. The package audit validates schema, identity, architecture entries, and ELF headers; it does not compare minGuideXOSVersion with a Server release.

The Server kernel version macros are compiled from kernel/core/include/kernel/version.h when referenced, but no build-time version-generation step, package-level Server version manifest, or checked-in mapping from that header to minGuideXOSVersion was found. The v0.5_DEVELOPER_STUDIO branch name is not a release tag: git describe --tags --always resolves to its commit, and no local v0.5 tag exists. The main Server checkout describes as v0.2-122-g9486b696 while its kernel header also declares 0.1.0.

## Local Server candidate matrix

Every local guideXOSServer* checkout with kernel/core/include/kernel/version.h that was inspected declares KERNEL_VERSION_STRING 0.1.0. No local kernel header declares 0.5.0 or higher.

| Candidate | Branch and commit | Version evidence | Native ELF / ABI | Build availability and qualification evidence | Assessment |
| --- | --- | --- | --- | --- | --- |
| D:/dev/guideXOS_DeveloperStudio_Tools | DEVELOPER_STUDIO_TOOLS, a13768dea9e5808baadb3c2908a8fca463505f16 | Header 0.1.0 | Manifest loader and Native ELF code are present. Existing production DeveloperStudio AMD64 ELF is 1,128,572 bytes, SHA256 1387CB09F2026252E4A277E96256C2BB59D06AE70ACFAA56A84EFEFF2F9CF845. This is not the accepted TOOLS12 artifact. | build.ps1, build-uefi.ps1, and scripts/smoke-compiler-bootstrap.ps1 exist. No build was run; no exact TOOLS12 guest qualification. | Version does not meet the declared 0.5.0 minimum if the header is the runtime version. |
| D:/dev/guideXOSServer | main, 9486b696b7cfd1f02e522e0b7d83385b8c5a9701 | Header 0.1.0; Git describes HEAD as v0.2-122-g9486b696 | Server mainline; no higher runtime version or TOOLS12 ABI qualification was reviewed. | build.ps1 and build-uefi.ps1 exist; scripts/smoke-compiler-bootstrap.ps1 does not. No build was run. | Not a version-qualified candidate. |
| D:/dev/guideXOSServerV0.5_DEVELOPER_STUDIO | v0.5_DEVELOPER_STUDIO, 801b4763aa09ddd6cab06bf726610063ca0389de | Header 0.1.0; branch label says v0.5, but there is no v0.5 tag or separate runtime release declaration | Strongest technical candidate. Bare-metal AMD64 Native ELF loader and guidexos-c-abi-v1 runtime are present. | build.ps1, build-uefi.ps1, and scripts/smoke-compiler-bootstrap.ps1 exist. No build was run. Phase 27X records a QEMU-proven compiler-built GUI lifecycle; Phase 28N records three fresh packaged QEMU boots of the Developer Studio editor/debugger integration. Those records qualify related runtime paths, not the accepted TOOLS12 ELF itself. | ABI/file-format fit is supported; numeric minimum-version satisfaction is not established. |
| D:/dev/guideXOSServerV0.5_PHASE4R1_QAC_ADMISSION | codex/qac-phase4r1-admission, a7b78a527b85fb30f810c414fa6c21e15fba1895 | Header 0.1.0 | Contains the focused QAC acceptance launcher integration. | build.ps1, build-uefi.ps1, and scripts/smoke-compiler-bootstrap.ps1 exist. No build was run. Existing records cover other Developer Studio phases, not TOOLS13R1. | Does not resolve the Server version contract. |
| D:/dev/guideXOSServerV0.5_PHASE30B_OWNER_RUNNER | codex/phase30b-retained-owner-runner, c2dada8520c58f7ebb9d80f35477cb81211a4256 | Header 0.1.0 | QAC owner-runner branch; hosted simulations are not guest qualification. | build.ps1, build-uefi.ps1, and scripts/smoke-compiler-bootstrap.ps1 exist. No build was run. | Does not provide a higher-version runtime. |
| D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT | v1.1_DOTNET_SUPPORT, 537274263a4c2c81b8da0b1288f2e3ce72a19ae0 | Header 0.1.0 | .NET Support lane; not an independent Developer Studio runtime release candidate. | build.ps1 and build-uefi.ps1 exist; scripts/smoke-compiler-bootstrap.ps1 does not. No build was run. | Does not resolve the minimum-version mismatch; lane retains QEMU priority. |
| D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT_qac_phase2 | codex/qac-c176-retained-session, 79070db55997cd867ee79134ee505b39ea5df35d | Header 0.1.0 | QAC integration snapshot, not a Server release candidate. | build.ps1 and build-uefi.ps1 exist; scripts/smoke-compiler-bootstrap.ps1 does not. No build was run. | Does not resolve the minimum-version mismatch. |

Other local Server architecture and feature checkouts also declared 0.1.0 in their kernel version headers. No alternate local Server candidate with an authoritative runtime version satisfying 0.5.0 was found.

### Strongest candidate’s native runtime fit

The v0.5 Developer Studio Server branch has actual bare-metal Native ELF support, not only the hosted experimental runtime. Its shared contract fixes the AMD64 image base at 0x50000000. The app runtime accepts prelinked applications up to 2 MiB and a mapped image up to 16 MiB, with at most four PT_LOAD segments and eight program headers.

The accepted TOOLS12 AMD64 artifact has:

- ELF64, little-endian, ET_EXEC, AMD64; no section headers.
- Entry point 0x500d36f0, inside the executable PT_LOAD segment.
- Image base 0x50000000, matching the Server Native ELF contract.
- Three PT_LOAD segments and five total program headers; no PT_INTERP or PT_DYNAMIC.
- File size 1,262,228 bytes, below the 2 MiB app-loader limit.
- Loaded address span 0xF582A0 (16,089,760 bytes), below the 16 MiB app image limit.
- Manifest entry point gx_main, ABI guidexos-c-abi-v1, and runtime native-elf.

The candidate SDK’s abi.h declares GX_ABI_NAME as guidexos-c-abi-v1 and uses the gx_app_context / gx_host_calls layout. The candidate kernel initializes the v1 host table and its bare-metal extension slots for the Developer Studio calls inspected in the source, including logging, window creation, text/rectangle drawing, event polling, workspace file operations, build/run/debug services, and document activation. No missing interface was identified in this source-level comparison.

This is strong evidence of file-format and API compatibility with the candidate runtime. It is not proof that the declared guideXOS minimum is met, and no guest run of the exact accepted TOOLS12 ELF was found. The qualification documents cited above use different package/source revisions or fixtures.

## TOOLS12 application artifact provenance

The accepted TOOLS12 AMD64 ELF was verified in both existing package copies:

| Copy | Size | SHA256 |
| --- | ---: | --- |
| D:/dev/guideXOS_DeveloperStudio_Tools_Source/out/TOOLS12/amd64-acceptance-stage/Apps/DeveloperStudio/bin/amd64/developerstudio.elf | 1,262,228 | 9CBC32163417A553B38A1F01EF69CDBAF49B99C58B9D63D0F06E2E9A31C1AC16 |
| D:/dev/guideXOS_DeveloperStudio_Tools_Source/out/TOOLS12/dual-architecture-package/Apps/DeveloperStudio/bin/amd64/developerstudio.elf | 1,262,228 | 9CBC32163417A553B38A1F01EF69CDBAF49B99C58B9D63D0F06E2E9A31C1AC16 |

Both package manifests are byte-identical to source app/app.json, SHA256 47A9C855AE32A372CC7674350A9E7255BB58927FF943798CC929F6EDFCBCF1BE. The manifest identifies com.guidexos.developerstudio, application version 0.1.0, schema 1, minimum guideXOS version 0.5.0, AMD64 NativeElf entry gx_main, guidexos-c-abi-v1, and native-elf runtime.

The TOOLS12 report records TOOLS12_HOSTED_VERIFIED, real hosted UI acceptance, Debug/Release suites, and isolated AMD64/ARM64 package audits. It does not qualify native Server guest behavior. The TOOLS12 implementation commit is de921d3d2ba49f630295443d2236dfa7da34fec0. Later source commits through 48f2da5 changed documentation only, so TOOLS12 remains the right application candidate for a future guest run. No rebuild was performed. The existing ELF was not replaced, and no separate build-provenance manifest binds its bytes to a specific compiler invocation.

The Server integration checkout’s production AMD64 ELF remains unchanged at 1,128,572 bytes and SHA256 1387CB09F2026252E4A277E96256C2BB59D06AE70ACFAA56A84EFEFF2F9CF845.

## Resolution and blocker

**Outcome D — COMPATIBILITY_UNRESOLVED.** No evidence shows a genuine ABI incompatibility: the candidate runtime exposes the v1 interfaces, the accepted ELF fits its loader contract, and adjacent guest qualification exists. At the same time, no local Server candidate has an authoritative runtime version at or above 0.5.0, and the loader does not enforce or explain the package minimum.

The source discrepancy is identified, but it is not proven to be metadata-only. The 0.1.0 value comes from a kernel version header that is not consumed by a checked-in version-reporting path; 0.5.0 comes from the application’s declared minimum runtime field. The v0.5 branch name alone cannot establish that the runtime is version 0.5.0. Therefore there is not enough evidence to change either value or to label the candidate safe.

The smallest legitimate next step is for the Server platform owner to state the canonical runtime version for the exact candidate commit and how minGuideXOSVersion is meant to map to it. If 0.1.0 is stale metadata, the Server owner should reconcile the Server version source through its authorized release process while preserving the application minimum. If the runtime is genuinely below 0.5.0, provide a qualified Server candidate whose declared runtime version satisfies that minimum. Do not lower the package minimum or bypass a compatibility check. The Server engineering owner controls the kernel/runtime version declaration; Developer Studio Tools owns app/app.json and its generated project default.

No compatible guest image was prepared. The candidate branch has build and QAC-aware smoke scripts, but existing ESP and out artifacts were not treated as a fresh isolated image for this package.

## QAC coordination and guest admission

The supported coordination interface is the QAC CLI documented in D:/dev/guideXOS_QAC/README.md. The preflight used only read-only status, list, and queue commands:

- Coordinator online: guideXOS QAC 2.0.0, PID 8204.
- QEMU processes: 0, including 0 foreign.
- Reservations: 0; managed sessions: 0; queued: 0; blocked: 0.

The live idle census does not release the .NET Support priority baton. QAC’s latest local coordination closeout, docs/QAC_SHARED_QEMU_COORDINATION_PHASE4R2.md, says .NET Support retains first QEMU priority and guest acceptance needs a separately authorized handoff after that priority is cleared. The TOOLS13R1 source handoff record reports no explicit release or independent Tools-lane authorization. This is an independent pending gate. No reservation was requested.

## Conditional guest-readiness plan

After the version contract and QAC baton are both resolved:

1. **Build an isolated image.** Use a disposable checkout/worktree at the owner-approved Server commit and a new output/staging directory. Record the exact source commit, toolchain, build commands, and input/output hashes. Do not build into or reuse a production Server package, tracked ESP, or existing guest image.
2. **Stage and verify the application.** Copy the accepted TOOLS12 app.json and AMD64 ELF into the isolated image. Recompute both hashes, confirm the manifest fields, verify the ELF entry and loader limits, and run the package audit against the staging root. If a rebuild is required, retain it as a separate candidate with its own hash and qualification record.
3. **Deploy the disposable C++ fixture.** Copy the existing TOOLS13R1 fixture from the source checkout into a new guest workspace. Record its source hash and the exact guest-visible workspace path; verify the path before starting the UI checks.
4. **Acquire QAC admission.** Obtain explicit .NET Support release or independent Tools-lane authorization. Then use the documented QAC acceptance flow for one AMD64 child; do not launch QEMU while queued or without the grant.
5. **Run one bounded AMD64 smoke.** Cap the guest runtime at 360 seconds. Verify Developer Studio startup, Tools menu, Go to File, Go to Line, Task List scan/navigation, and Navigate Back/Forward. Capture a screenshot plus serial and host traces for the same run.
6. **Shut down and release.** Request graceful QMP shutdown, verify QEMU exits successfully, wait for QAC to report the reservation Released, and confirm the coordinator has no remaining owned session. Preserve the image hash, app/manifest hashes, serial log, screenshot, and QAC trace; discard only the disposable image after evidence is secured.

No build, package overwrite, Server source edit, QAC reservation, branch change, or QEMU launch was performed during this preflight.
