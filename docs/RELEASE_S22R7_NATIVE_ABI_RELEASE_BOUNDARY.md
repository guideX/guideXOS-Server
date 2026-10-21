# S22R7 Native ABI Compatibility and Release Boundary

Date: 2026-10-20

## Outcome

**Outcome C — ARCHITECTURAL_BLOCKER.** The release build has a fail-closed minimum-version preflight for the pinned Developer Studio package, but the v0.3 runtime Native ELF loader does not enforce that minimum. More importantly, the v0.3 and v0.5 host tables share the same ABI name and API version despite incompatible layouts. The runtime has no trustworthy package-to-ELF identity, declared ABI table-size requirement, or service negotiation contract with which to reject every incompatible binary before entry.

No runtime or ABI changes were made. A one-off manifest comparison would only enforce an unsigned declaration and would not establish that the paired ELF matches it. The current production manifest scanner also uses bounded substring extraction rather than a structural JSON parser. Adding a claimed ABI-size requirement would invent a contract absent from the package. The compatibility gap therefore remains for direct runtime loading; the official v0.3 release build gate still rejects the pinned Developer Studio pair.

This result does not qualify the current v0.5 Developer Studio package for v0.3 and does not establish hostile-app isolation.

## 1. Repository preflight and provenance

At the start of this work:

| Check | Result |
|---|---|
| Branch | `main` |
| HEAD | `fb807a7cf18b1d82ca0c3fc1b934dae317be0061` (`docs: reassess S22R6 editor adapter trust boundary`) |
| Expected S22R6 checkpoint | Exact HEAD; checkpoint is an ancestor |
| Tracked/staged worktree changes | None |
| Untracked paths | 854, all pre-existing; includes `build/s22r6_native_abi_layout_test.exe` |

The v0.3 server checkout was the only checkout used. The modern Developer Studio and v0.5 reference checkouts were not opened or modified. S22R5 and S22R6 findings were used from their committed reports and recorded evidence. No branch, worktree, release marker, package, ISO, or QAC reservation was changed. No push or guest run was performed.

## 2. Current compatibility checks

The production bare-metal path is `kernel/core/native_elf_baremetal.cpp`; the hosted resolver/executor is a separate development path. The hosted path has additional checks but is not the v0.3 production guest loader.

| Contract | v0.3 bare-metal behavior | What that establishes |
|---|---|---|
| Package identity | Reads `id` and `displayName` from `/Apps/<directory>/app.json`; lookup accepts the self-declared id, display name, or directory name. No signature, manifest/ELF digest binding, or directory-to-id rule is checked. | A display/lookup label only. The manifest ID is not an authenticated security principal. |
| Minimum Server version | `minGuideXOSVersion` is not read by `parse_package`. The hosted manifest validator and launch resolver also do not compare it to a runtime version. | The runtime accepts a package that truthfully declares a newer minimum if its other checks pass. |
| Release-time version gate | `build.ps1` checks the exact pinned Developer Studio manifest/ELF pair and calls `Assert-DeveloperStudioMinimumServerVersion` against target `0.3.0` before build cleanup. Release receipt validation repeats the minimum-version comparison. | The official build path rejects the pinned `0.5.0` package. This is not a runtime loader check and does not protect packages placed on `/Apps` by another path. |
| Manifest architecture | The kernel scanner extracts an entry's `architecture` text and requires `amd64`; it does not validate the top-level `supportedArchitectures` array. Its string scanner is not a full JSON parser. | The selected manifest entry claims AMD64. This is not package identity or code provenance. |
| ELF architecture and image | The bare-metal ELF loader requires ELF64, little-endian, `ET_EXEC`, machine 62 (AMD64), bounded program headers and load image, an executable load segment, and `e_entry` inside the overall loaded image range. It does not prove `e_entry` lies inside an executable segment. Dynamic/interpreter loading is not supported. | The file is a loadable AMD64 static image under the implemented loader subset; the entry permission check is incomplete. |
| Native ABI identity/version | The entry text must equal `guidexos-c-abi-v1`; the host table and context advertise `GX_API_VERSION == 0`. There is no ABI version field in the manifest. | Only the shared name and host-reported version are matched. They do not identify a concrete table layout. |
| Host table size and callbacks | The v0.3 runtime zeroes `gx_host_calls`, sets `size = sizeof(gx_host_calls)` (256 bytes) and version 0, then installs supported callbacks. Several declared slots remain null, including workspace, build, Run, Debug, and document-activation callbacks. The loader does not negotiate a required table size or callback set with the app. | `size` describes the v0.3 table, not the services a package requires. Callback availability is an app-side convention; it is not verified before entry. |
| Manifest entry point | The manifest string must equal `gx_main`. | The loader then calls the ELF header's `e_entry` as `gx_main`; it checks that address is inside the overall loaded image but does not require an executable segment at that address, resolve a symbol named `gx_main`, or prove the manifest label matches `e_entry`. |

The kernel's `kernel/core/include/kernel/version.h` reports component version `0.1.0`, while the release scripts use product target `0.3.0`. A runtime minimum-version comparator would need a canonical product-version source before it could make a reliable version claim. The current package scanner's substring-based `json_string` helper also does not structurally bind a found string to a top-level JSON field.

In the current `Apps` tree, the six non-Studio manifests declare minimum `0.1.0`; Developer Studio declares `0.5.0`. Their first entries declare AMD64, `gx_main`, and the shared `guidexos-c-abi-v1` label. These declarations show which known package metadata aligns with the v0.3 release target; they do not prove an ELF's identity or ABI use.

### Can a v0.5 application enter the v0.3 runtime?

**Yes, if it is placed in `/Apps` with a manifest and AMD64 ELF satisfying the bare-metal parser's other checks.** The v0.5 Developer Studio contract documented in S22R5/S22R6 declares minimum Server `0.5.0`, but the runtime ignores that field. Its AMD64 entry uses `gx_main` and `guidexos-c-abi-v1`, matching the v0.3 parser's checks. The matching ABI name and `GX_API_VERSION == 0` do not distinguish the layouts.

The S22R5/S22R6 measurements show a 256-byte v0.3 host table and a 456-byte v0.5 table. At offset 240 v0.3 has `play_pcm`, while v0.5 places a build callback there; v0.5 file callbacks begin at 264 and activation is at 448. A v0.5 app can therefore interpret a v0.3 callback slot with a different meaning or read beyond the v0.3 table if it assumes its compiled layout. The v0.3 loader does not inspect or validate such app-side accesses.

This is a static code-path conclusion, not a guest launch result. The current v0.3 build script rejects the pinned Developer Studio package before packaging, but that release gate does not turn the runtime manifest into a trusted identity or prevent manual package staging.

## 3. Changes and validation

No production loader, manifest, ABI, kernel, or application code changed. No ABI-size field was invented. The existing v0.3 prefix and callback offsets remain untouched.

Validation performed:

- Ran the pre-existing hosted ABI layout executable `build/s22r6_native_abi_layout_test.exe`: **PASS** (`Native ABI layout test PASS`). It verifies current structure offsets and the 256-byte v0.3 table.
- Reviewed the release minimum-version helper and its call sites in `build.ps1` and `scripts/release-build-integrity.psm1`; both target Server `0.3.0` and reject the pinned Developer Studio `0.5.0` minimum in the build/receipt flow.
- Inspected current package manifests and loader code paths. No dedicated runtime manifest-compatibility test harness exists in the current tests.

Not run: malformed-manifest execution tests, freestanding kernel build/link, QEMU, or guest acceptance. No loader code changed, and the existing test does not exercise manifest parsing or the kernel runtime. No guest acceptance is claimed from hosted results.

## 4. Developer Studio release disposition

- Modern Developer Studio remains an active development project.
- An editor-only profile remains a possible future integration target after its product behavior and trust model are defined.
- Server v0.3 runs Native ELF applications in kernel privilege with a separate app stack but no per-app address space or syscall boundary. It cannot enforce a hostile-app-resistant workspace boundary.
- Disabling Build, Run, and Debug controls would only change UI/action availability. It would not establish ABI compatibility, a trusted app identity, or workspace security.
- The existing Server v0.5 Studio package, which declares minimum Server `0.5.0` and uses the incompatible 456-byte ABI layout, must not be represented as Server v0.3-compatible.
- Any future trusted first-party editor approach needs its own documented trust model and explicit approval. It must not silently weaken the S22R6 isolation requirement.

## 5. Native ELF isolation prerequisites

### Essential security prerequisites

1. **Privilege separation:** Native applications must run in user mode. Kernel-only state, page tables, device access, and privileged instructions must be unavailable to app code.
2. **Per-application address spaces:** Give each app isolated mappings with executable, writable, and user permissions enforced by the MMU. Keep kernel mappings inaccessible; validate load regions and tear them down on exit.
3. **Kernel-mediated calls:** Replace direct kernel function-pointer callbacks with a syscall or similarly trapped ABI. Copy bounded inputs and outputs across the boundary; validate lengths, pointer ranges, operation numbers, and results in the kernel.
4. **Trusted package identity:** Load a manifest and ELF as one verified package identity, using a kernel-controlled registry or verified signatures/digests. Bind the verified identity to the bytes actually executed. An unsigned manifest ID or permission list is descriptive metadata only.
5. **Versioned ABI and service negotiation:** Define a distinct ABI identity for incompatible layouts, a fixed prefix size, and explicit optional/required service negotiation. Reject unsupported required ABI versions or services before entry. Preserve the known v0.3 256-byte prefix for applications that must remain supported; do not infer v0.5 compatibility from the shared `v1` label.
6. **Generation-bound capabilities:** Bind handles to the owning application and a generation. Validate ownership on every operation, revoke all capabilities on teardown, and prevent handle reuse from reviving stale authority.
7. **Workspace-root authorization:** Issue a workspace capability only after policy authorizes a root. Resolve paths within that root after normalization, account for mount aliases and traversal, bound path and buffer sizes, and prevent escape to unrelated VFS roots.
8. **Deterministic invalid/stale behavior:** Return a documented denial or stale-handle result without partial writes, kernel faults, or use-after-free. Define side-effect and cleanup behavior for every failure path.
9. **Compatibility and migration policy:** Keep existing known-good native apps on an explicit legacy contract or require a deliberate rebuild/migration. Test old table prefixes and the new negotiated ABI independently; never silently relabel a binary or lower its declared minimum.

### Optional enhancements after the security boundary

- Atomic workspace saves and crash recovery; useful for editor data integrity but not a substitute for identity or authorization.
- Per-app CPU, memory, and file quotas, scheduling, and operational telemetry beyond the minimum bounds required for safe calls.
- Richer permission prompts, audit history, and capability delegation once the kernel enforcement model exists.
- Broader project/build/run/debug services. They are product features, not prerequisites for the basic isolated read/write workspace boundary.

## 6. Remaining risks and blockers

- The runtime can discover and launch a manifest-declared v0.5 package when the binary satisfies its current AMD64 static ELF checks; its minimum version is not enforced at runtime.
- The manifest has no ABI version, table-size, or required-service declaration. The host table size cannot safely be used to infer what the application will dereference.
- The package manifest is unsigned and not bound to the ELF by the runtime. Its ID and permissions cannot serve as a non-forgeable application identity.
- Apps execute with kernel privileges. They can bypass callbacks and directly access kernel memory, so callback checks cannot enforce a hostile-app boundary.
- The loader calls `e_entry` based on a manifest string, does not verify a `gx_main` symbol contract, and does not require `e_entry` to fall in an executable load segment.
- The component version header and release product target disagree (`0.1.0` versus `0.3.0`), and the production manifest reader is not a structural JSON parser.
- No runtime rejection matrix or freestanding build/link validation was produced in this audit. No QAC-managed guest acceptance was attempted.

## 7. Recommended next Main Integration step

Authorize a design-only Native ELF isolation and ABI-contract phase before adding workspace callbacks or a Developer Studio adapter. That phase should resolve the canonical product version source, define verified package-to-ELF identity and a new size/capability-negotiated ABI identity, and specify user-mode/address-space/syscall lifecycle. Then implement and qualify the kernel boundary while preserving the legacy v0.3 ABI prefix. Revisit an editor-only Studio profile only after that boundary and its trust model receive explicit approval.

## Repository result

The only tracked file added by S22R7 is this report. A local documentation commit is recorded in Git history. The 854 pre-existing untracked paths, including the ABI test executable, remain untracked and preserved. No push was attempted.
