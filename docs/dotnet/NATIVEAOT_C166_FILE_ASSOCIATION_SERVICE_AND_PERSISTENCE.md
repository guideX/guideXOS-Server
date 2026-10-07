# NativeAOT C166 — File Association Service and Persistence

## Status

Implementation work is present, but C166 acceptance is **not yet validated**.
No Outcome A claim is made. The existing C164 runner stops at its C163 product
hash gate before compiling or booting C166. The required mutation, reboot,
failure, ordinary-boot, stress, and regression evidence remains outstanding.

Starting repository HEAD was `49986ddcd14801c526d774676fc362648d653da9`,
which is the C165 audit-only commit on `v1.1_DOTNET_SUPPORT`. This is newer
than the prompt's stated C164 HEAD. Its only change from C164 is the C165 audit
document; that document remains intact and committed. The worktree was clean
at the start.

## Authority and behavior

`kernel::appmodel` in `kernel/core/app_launch_target_resolver.cpp` remains the
single association authority. Its immutable compiled table remains capacity
16 with one entry: `.txt` to `com.guidexos.apps.managed.notes`. Overrides use a
separate one-record fixed array, since `.txt` is the only known configurable
key. The record is 116 bytes: extension[16], state (u32), and app ID[96].
Runtime override storage therefore costs 116 bytes, excluding alignment.

The three states are `NoOverride`, `ApplicationOverride`, and `Disabled`.
Extensions are restricted to known compiled keys, normalized to lower-case
ASCII, and matched case-insensitively by the existing final-suffix resolver.
The only eligible pair is `.txt` plus the canonical Managed Notes ID. Other
registered apps are not accepted. Native Notepad remains audit-only and is not
eligible for C164 document activation.

The resolver checks a valid override before the compiled entry. Disabled
returns no association. Reset writes an empty override image, restoring the
compiled Notes default. Both File Explorers still use this same resolver; the
managed Explorer continues to request native file activation.

## Managed boundary and ABI

The C164 App Model request context is activation-specific and is not repurposed.
The managed host table advances from v3/120 bytes to v4/128 bytes by appending
one `associationService(request, response)` callback at offset 120. v1 (104),
v2 (112), v3 (120), snapshot offset 104, and close offset 112 remain preserved.
Managed code checks both version >= 4 and size >= 128 before reading the tail;
ABI v3 therefore reports `NotSupported` without dereferencing it. The callback
uses fixed request/response buffers and operation codes Query, SetOverride,
Disable, and Reset. The reusable `GuideXosFileAssociations` wrapper exposes
bounded calls and does not access the backing file.

Response fields contain normalized extension, compiled default, override
state, override ID, effective ID, and association-presence flag. Service
statuses distinguish success, unsupported, invalid argument, unknown
extension, ineligible handler, capacity exceeded, persistence failure, and
invalid persisted state.

## Persistence

Persistence belongs to the native association authority. The backing path is
`/GXASSOC.BIN` on the writable root FAT VFS mount. Settings persistence stays
format v2; association persistence has its own v1 format. The image is fixed at
124 bytes: 8-byte header (magic `GSA1`, version u16, count u16) and one 116-byte
record. The record key is the normalized extension; it contains no path,
filename, or display name. Application identity is stored canonically.

Missing file means no override. Invalid size, magic, version, count, state,
extension, or app identity rejects the complete image and falls back to
compiled defaults. Startup loads the image after VFS mount and before desktop
activation; invalid stored state emits the bounded
`[APP-MODEL] association-persistence=invalid-ignored` diagnostic. An app that
is missing or ineligible is rejected on load, so it cannot be launched.

Mutation validates the operation, key, and handler, serializes a complete
candidate, writes it through native VFS, reads it back, validates it, and only
then installs runtime state. A write or read-back failure leaves the current
runtime override unchanged and returns `PersistenceFailed`. VFS `write_file`
is synchronous in this repository; it has no separate flush callback.

## Verification recorded so far

- Managed HostLogProof build: succeeded, 0 errors; existing MSBuild path and
  Notes analyzer warnings remain.
- Kernel AMD64 link with the production C164 phase defines: succeeded.
- `git diff --check`: succeeded.
- C164 phase runner: stopped before build/boot because current ESP/kernel
  hashes differ from the C163 manifest's expected restored hashes. No C166
  proof or ordinary boot was run.
- No C166 service, ABI, persistence corruption, failure injection, 1,000
  transaction, or managed wrapper test suite has been run yet.
- C164/C163/C162/C160/C156 regression suites, Settings smoke, Notes proof, and
  native/managed File Explorer mutation parity are therefore unverified for
  this implementation. C128 remains unverified.

No Settings UI was added. No proof-only association override was installed.
The production `.txt` state has not been proven by a fresh boot; with no
association image, compiled Managed Notes remains the intended default.

## C166R validation continuation (2026-10-05)

### Result

C166 remains **not accepted**. This continuation stopped before proof-media installation
or any QEMU mutation/reboot sequence. No C166R commit was created. The C165 audit
and C166 implementation commit remain unchanged.

### Gate and artifact-lineage findings

The inherited C164 runner (`scripts/dotnet/run-c158-managed-calculator.ps1 -PhaseC164`)
requires the current canonical kernel, ESP kernel, and ramdisk to equal the C163
manifest's post-phase hashes before it reaches any C164/C166-specific build or boot
logic. That is stale for C164 and C166. C163 records kernel
`02817323329C5CA7A6B0DAB67C8709CD60CDED9B3A4F2B096C9D1194E503BE4B` and ramdisk
`7D8EB9AABC6016BD2C8EBC2F33F5805BEDEE3F15368F9F31C4335C3D80E43266`.

The accepted C164 manifest (`out/dotnet/c164-file-activation/c164-proof-manifest.json`)
claims ordinary clean kernel `AAE19D63090854CB6389A8311FF797E406E4C016058FE9559F4A9EB584D3E1FB`
and ramdisk `5CEC7F5FF2B43AE223A9793232BE0740E33FE357FA8B07869A941BAB54A2B41D`.
Its six boot records use that pair, but the saved C164 canonical kernel and ESP-kernel
backups both hash to the C163 kernel (`028173...`), and the current protected ESP kernel
also hashes to `028173...`. The saved C164 ramdisk backup and current ESP ramdisk hash
to `7D8EB9...`, while the C164 accepted manifest claims `5CEC7F...`. Thus the C164
manifest's ordinary boot hash fields are not corroborated by its saved artifact backups
or current protected media. The actual C164 clean artifacts named by the manifest are
not available in the inspected evidence tree. C163 historical hashes, C164 claimed
ordinary hashes, the current C166-linked kernel (`F32ECE...` observed before the proof
link), and C166 proof/clean products cannot be safely treated as interchangeable.

No hash check was removed and no artifact was installed. The proper next harness repair
must verify accepted predecessor source identity and require actual product files whose
hashes match the predecessor manifest; it must also avoid C164-specific stress/final-state
conditions for C166. The existing combined runner is not yet a C166 runner.

### Validation attempted

- Managed C166-era NativeAOT proof composite build: succeeded after the first attempt
  found and fixed a missing `using System` in an experimental wrapper test. The test was
  then removed because it had not been executed; this compile is not an ABI or wrapper
  test pass and the final source does not contain that experimental test.
- C166-era kernel AMD64 link with the C164 proof defines: succeeded. The resulting
  ignored `kernel/build/amd64/bin/kernel.elf` was restored from the existing C164
  canonical backup. Current canonical kernel and ESP kernel both hash to
  `02817323329C5CA7A6B0DAB67C8709CD60CDED9B3A4F2B096C9D1194E503BE4B`; ESP ramdisk
  hashes to `5CEC7F5FF2B43AE223A9793232BE0740E33FE357FA8B07869A941BAB54A2B41D`.
- Existing C164 serial evidence was inspected, not freshly rerun: native resolver 28
  cases plus 1,000 resolutions; managed C160 snapshot 22/22 plus 1,000 calls; C162 ABI
  close wrapper 18 cases; C163 Explorer 48 cases plus 1,000 refreshes and 100 navigation
  operations; C164 activation context 30 cases; Notes state 32 cases. These remain
  historical predecessor evidence only.
- `git diff --check` passed after cleanup. PowerShell parsing passed for the experimental
  runner edit before it was reverted.

### Persistence architecture risk

Native persistence currently calls `vfs::write_file` followed by a fixed-image readback,
then commits runtime state. There is no separate flush primitive in this path; VFS
`flush()` is a no-op because writes are synchronous. However, the FAT `overwrite_path`
implementation writes candidate bytes into the existing file's cluster chain before
updating its directory entry and has no atomic replace/rollback. A storage error during
overwrite can therefore damage the previous persisted image. Runtime state remains
unchanged when `persistCandidate` reports failure, but the stronger requested guarantee
that the old persisted state remains authoritative is not established and may be false.
This requires a crash-safe VFS/FAT replacement strategy or a separately validated
redundant/dual-slot persistence design before Outcome A can be considered.

### Unverified C166R requirements

No fresh native service suite, managed wrapper suite, ABI-v4 executed layout suite,
persistence format/corruption/failure-injection suite, 1,000 mutation stress, resolver
state stress, real `/GXASSOC.BIN` write/readback evidence, reboot reload proof, File
Explorer Disabled/Reset parity, three proof boots, three ordinary boots, Settings smoke,
or clean production installation was run. No C166 acceptance evidence is claimed.
Settings v2 remains source-defined and unmodified; C128 remains unverified. No Default Apps
UI was added by this continuation.

## C166R2 recovery and dual-slot implementation (2026-10-05)

### Starting state and protected media

The implementation started at `1688a98deb1aa8714591bd09418fa22362307a86`
on `v1.1_DOTNET_SUPPORT`, tracking `origin/v1.1_DOTNET_SUPPORT`. The initial
status showed only this document modified; that existing C166R history was
preserved and extended. Initial ahead/behind was 0/0 (the earlier C166R report
of 1/0 was stale).

Before any proof-media installation, byte-for-byte backups were written under
`out/dotnet/c166r2-artifact-lineage/pre-run-protected/` and verified against
the source SHA-256:

| Pre-run protected product | SHA-256 |
| --- | --- |
| canonical `kernel/build/amd64/bin/kernel.elf` | `02817323329C5CA7A6B0DAB67C8709CD60CDED9B3A4F2B096C9D1194E503BE4B` |
| `ESP/kernel.elf` | `02817323329C5CA7A6B0DAB67C8709CD60CDED9B3A4F2B096C9D1194E503BE4B` |
| `ESP/ramdisk.img` | `5CEC7F5FF2B43AE223A9793232BE0740E33FE357FA8B07869A941BAB54A2B41D` |

These are pre-run rollback bytes only; they are not called C164 canonical
products. No proof product was installed, so no restoration was necessary.

### Historical artifact discrepancy and authority

Historical C163 kernel/ramdisk hashes remain as recorded in their manifest.
The C164 manifest's claimed ordinary kernel and ramdisk hashes are not
corroborated by its saved canonical backups or the protected media: its kernel
backup and protected kernels match the historical C163 kernel, while its
ramdisk backup/protected ramdisk match a value different from the C164 manifest
claim. The discrepancy is retained as evidence; neither manifest was changed.
Accordingly, C163 and C164 artifact hashes are retired as C166R2 build gates.
The intended authority is the committed source state, fresh proof products,
verified pre-run rollback backups, and fresh clean production products.

The inherited runner still contains a C164-specific C163 artifact-lineage
check and was not converted into a C166R2 proof runner in this continuation.
No fresh build, QEMU boot, or clean production install has therefore been
claimed. This infrastructure work remains an acceptance blocker.

### Association persistence format v2

The single-file `/GXASSOC.BIN` v1 overwrite path was replaced in native source
with `/GXAS0.BIN` and `/GXAS1.BIN`. The legacy v1 file is ignored; no automatic
migration is performed. Runtime authority remains `kernel::appmodel`, and the
managed service continues to call the native ABI rather than editing files.

Each slot has a fixed 136-byte image: a 16-byte header (`GSA2`, version 2,
record count, and 64-bit generation), the existing 116-byte override record,
and a 4-byte integrity field. The integrity field is CRC-32/ISO-HDLC
(reflected polynomial `0xEDB88320`, initial and final XOR `0xFFFFFFFF`) over
the preceding 132 bytes. Total slot footprint is 272 bytes. Generations start
at 1 and increase monotonically; at `UINT64_MAX`, another mutation is rejected
as persistence failure rather than wrapping.

Startup validates each slot independently, chooses the only valid image or
the valid image with the highest generation, and deterministically chooses A
on equal generations. Missing slots mean compiled defaults. A corrupt slot is
ignored when the other is valid; if neither is valid, compiled defaults remain
usable. A mutation writes only the other slot, reads back the exact fixed image,
validates it and compares all bytes before committing runtime state. Thus a
partial candidate write cannot overwrite the slot selected as current before
that transaction. This does not claim FAT atomic replacement or power-loss
linearizability; an I/O failure after a complete new image reached storage can
leave acknowledgement ambiguous.

A freestanding GCC syntax-only compile of `app_launch_target_resolver.cpp` passed. A full kernel link and the required
fault-injection matrix, 30+ focused slot cases, generation exhaustion fixture,
1,000-mutation stress, managed wrapper/ABI tests, reboot persistence, real FAT
recovery, explorer parity, regression suites, proof boots, and three ordinary
boots remain unverified. No test totals or Outcome A claim are made. The final
C166 disposition is **not accepted** pending those gates. Host ABI target
remains v4/128 bytes with callbacks at 104/112/120; Settings remains v2; C128
remains unverified; no Default Apps UI was added.

## C166R3 validation-lane reconstruction (2026-10-05)

### Starting state and runner audit

C166R3 started at the expected full HEAD `72b2673e84212e62b78ea1835e583e34bb0f5d23`
on `v1.1_DOTNET_SUPPORT`, tracking `origin/v1.1_DOTNET_SUPPORT`. The starting
worktree was clean and the branch was 1 ahead / 0 behind. The latest commit is
the C166R2 dual-slot hardening commit `72b2673e`; C166 implementation remains
`1688a98deb1aa8714591bd09418fa22362307a86`. C166 had not been accepted, so the
zero-modification accepted-state stop did not apply.

The inherited entry point remains
`scripts/dotnet/run-c158-managed-calculator.ps1`. Its phase coupling is explicit:
C164 implies C163, then C162, C161, and C160. It retains common build, protected
media backup/restore, QEMU/QMP launch, serial waiting, hashing, and manifests,
but its C166-inappropriate gates and scenarios include:

- C163-only predecessor outcome/ABI/artifact hashes before the phase build;
- C164 assumptions about accepted C163/C162 manifests and exact restored
  product hashes;
- C163 menu counts, Explorer registration/refresh/navigation proof markers;
- C164 activation-context and Notes document-state proof, fixture navigation,
  and 25 distinct File Explorer-to-Notes lifetimes;
- historical product identity and post-phase restoration fields tied to C160-C164.

C160-C164 lifecycle stages are reusable only when those regressions are
explicitly requested and freshly executed. C166 needs its own association
state, persistence, and reboot acceptance assertions. Historical C163/C164
manifests are retained as regression evidence and are not prerequisites.
The new entry point is `scripts/dotnet/run-c166-file-associations.ps1`; it
checks the existing branch, discovers tools, builds output-only proof products,
and makes proof boot an explicit opt-in. It has no C163/C164 hash gate. Its
current proof-boot body deliberately reports blocked because complete C166
state-transition scenarios have not yet been connected.

### Environment discovery and protected media

The repository AMD64 Makefile chooses plain `g++`/`ld` on Windows, so the
successful production kernel toolchain is the installed MinGW fallback rather
than the `x86_64-elf-*` family. Exact paths and versions discovered:

| Tool | Path | Version/result |
| --- | --- | --- |
| .NET SDK | `C:\Program Files\dotnet\dotnet.exe` | 10.0.401 |
| MinGW g++ | `C:\mingw64\bin\g++.exe` | 15.2.0, `x86_64-w64-mingw32` |
| MinGW ld | `C:\mingw64\bin\ld.exe` | 2.46.0.20260210 |
| MinGW objcopy | `C:\mingw64\bin\objcopy.exe` | 2.46.0.20260210 |
| Make | `C:\mingw64\bin\mingw32-make.exe` | available |
| NASM | `C:\mingw64\bin\nasm.exe` | 2.16.03 |
| QEMU | `C:\Program Files\qemu\qemu-system-x86_64.exe` | 11.0.0 (v11.0.0-12122-ga4bb4b10c9) |

The named `x86_64-elf-gcc/g++/ld/objcopy` tools were not found in PATH or the
repository's configured candidate location; the production toolchain is still
available through the documented Windows MinGW fallback. The initial C166R3
environment record used an incompatible QEMU `--version` switch and said
unavailable; this was corrected to QEMU's `-version`, which returned the
version above.

Before the full proof kernel rebuild, canonical kernel, ESP kernel, and ESP
ramdisk hashes were respectively `02817323329C5CA7A6B0DAB67C8709CD60CDED9B3A4F2B096C9D1194E503BE4B`,
`02817323329C5CA7A6B0DAB67C8709CD60CDED9B3A4F2B096C9D1194E503BE4B`, and
`5CEC7F5FF2B43AE223A9793232BE0740E33FE357FA8B07869A941BAB54A2B41D`. The
ignored canonical kernel output was restored from the verified C166R2 byte
backup after building. All three protected hashes were rechecked and match.
No proof media was installed on ESP. No restoration of ESP bytes was needed.

QEMU process audit found PID 29980 running a separately named
`guideXOS-v03-scifi-final-candidate` workload from
`D:\dev\guideXOSServerV0.3_SCI_FI_THEME`. It is unrelated to this repository.
It was left running. QEMU boots and all guest-executed cases are blocked until
that independent workload ends; QEMU has not been declared unavailable.

### C166R3 source/test additions and executed checks

Added native C166 association-service cases for clean query/default,
case-normalized extension, Set, Disabled, Reset, unknown extension, malformed
operation, ineligible Notepad/empty handlers, null arguments, repeated
mutations, monotonic generation observations, and 200 mutation stress
operations. Added a managed wrapper proof for v1/v2/v3/v4 sizes, callback
offsets, v3/short/malformed ABI rejection, absent callback, capability checks,
native result mapping, invalid arguments, and malformed response rejection.
These suites compile into the fresh proof composite but remain **not executed**
because their current invocation path requires a guest boot.

Executed successfully:

- PowerShell parse checks for the new runner and inherited runners;
- XML parse of the managed project;
- MinGW syntax-only compile of the modified native association resolver;
- managed C# build with C155/C156/C157/C160/C163/C164/C166 proof defines:
  0 errors, existing MSBuild base-intermediate-path and unreachable-code
  warnings;
- fresh NativeAOT proof composite build from current source, with existing
  linker/PDB/entry-point warnings;
- fresh proof ramdisk generation;
- full AMD64 kernel compile, link, and PE-to-ELF conversion using the current
  source and configured MinGW toolchain; repository warnings remain, including
  the existing desktop line-buffer diagnostic.

Fresh product hashes (proof products only):

| Product | SHA-256 |
| --- | --- |
| NativeAOT proof composite | `C24B84128EDABA027B92A78F2ED1674CCF551ED31905BDCB55D98715924A7DCA` |
| Proof kernel | `4B0A75B3C347A21E7AC9045EBC6F3D9CBF7F5F30DD65134CBDC01260122A3DE5` |
| Proof ramdisk | `DB6A880E78DDC24749AE470CFAFDA6F68AEE9D1A5D10438052EA15D3B59E5344` |

The output manifest is `out/dotnet/c166r3-file-associations/c166r3-proof-manifest.json`.
It is ignored build evidence, not committed source. It records current HEAD and
fresh products, not C163/C164 historical hashes. No fresh production composite,
clean production kernel/ramdisk installation, serial log, proof boot, ordinary
boot, or regression suite was produced. Native focused test total, persistence
format/corruption/partial-write tests, ABI executed total, managed executed
total, CRC vector, slot-selection/tie/recovery evidence, C164/C163/C162/C160/C156
regressions, Explorer parity, Notes activation, Settings smoke, C128, and
reboot persistence are all unverified. The C166 outcome remains **not
accepted**; the present phase classification is Outcome E (an unrelated
existing QEMU run prevents guest execution), not a source/toolchain failure.

The slot contract remains the C166R2 baseline: `/GXAS0.BIN` and `/GXAS1.BIN`,
v2 136-byte images, 16-byte headers, 116-byte records, CRC-32/ISO-HDLC, 64-bit
nonwrapping generations, highest-valid selection with slot A on ties, and
runtime update after write/readback validation. Legacy `/GXASSOC.BIN` remains
ignored without migration. No Settings Default Apps UI was added; Settings
format remains v2; C128 remains unverified.

## C166R7 — Production Module Extraction and Host Validation (2026-10-06)

C166R7 started from `b9a6aa0765d6f71dea240d990d3542b86b9dfb99` on `v1.1_DOTNET_SUPPORT`, with upstream `origin/v1.1_DOTNET_SUPPORT`, a clean worktree, and 0/0 tracking. No newer accepted C166 work was present.

C166R6 did not find a trustworthy host lane because association tests and persistence lived inside `app_launch_target_resolver.cpp`. Compiling that translation unit pulled in VFS, launch-target parsing, and unrelated kernel/application headers. Excluding those dependencies with broad host preprocessor guards or reimplementing persistence would have broken source parity or duplicated the algorithm.

The C166 authority has been extracted to `kernel/core/file_association_service.cpp`, with its declarations and bounded storage callback in `kernel/core/include/kernel/file_association_service.h`. The module owns the compiled table, resolution, extension normalization, eligibility, service operations, serialization, CRC, slot validation and selection, generation, persistence transaction, and startup load. The resolver now retains only VFS stat/classification and launch-target behavior and calls the extracted resolver. No association algorithm was copied into the host adapter.

`kernel/core/file_association_vfs_storage.cpp` supplies the production adapter and preserves `/GXAS0.BIN` and `/GXAS1.BIN`. The VFS `write_file` completes its path operation before returning and offers no path-level flush, so the adapter's flush operation is a successful no-op after the write. Kernel startup installs the adapter after mount and before association load/file activation. The host fake storage is `samples/host/C166Association/fake_storage.h`; it stores independent slots and supports short writes, read failures, flush failures, and corrupt readback.

Host ABI remains v4 / 128 bytes. The managed host assertions check v1/v2/v3/v4 sizes 104/112/120/128 and offsets 104/112/120. Persistence remains format v2 with 16-byte header, 116-byte record, CRC at byte 132, 136-byte slots, and 272-byte total footprint. The CRC implementation was factored into one production helper without changing CRC-32/ISO-HDLC behavior: `123456789` produces `0xCBF43926`, and empty input produces `0x00000000`.

The current host run compiled and linked the production service translation unit directly into a native executable with the fake storage callbacks. It reported 436 passing native checks, including clean/default query, override, disable, reset, invalid handler/extension, both-corrupt fallback, equal-generation tie behavior, alternation, generation exhaustion, and failure handling. The exhaustive partial-write loop exercised all sizes 0 through 135 (136 cases); each rejected the mutation, preserved the authoritative slot bytes, and reloaded the previous default state. The full-write control advanced generation to 3. Persistence stress ran 1,000 transactions and resolver stress ran 1,000 lookups.

The managed host project `samples/host/C166Association/ManagedHostTests.csproj` links the repository's `NativeAbi.cs`, C166 wrapper, and C166 managed test source. It passed 22 cases, including wrapper operations/status handling and ABI layout. These wrapper fixture tests do not claim to test native persistence. Native/managed agreement fixtures and the complete named dual-slot validation matrix have not been completed. In particular, all requested corruption variants, independent read/reopen failure points, and persistent stress CRC/generation auditing still need dedicated cases. Therefore the host acceptance marker was deliberately not emitted and C166 host validation is not complete.

The host runner is `scripts/dotnet/run-c166-file-associations.ps1 -HostTestsOnly`. It does not inspect QEMU or run media setup. It writes `out/dotnet/c166r7-host-validation/c166r7-host-manifest.json` and per-suite logs. The current manifest marks the host suite incomplete.

The proof composite build was attempted from the same source HEAD and failed at NativeAOT link with unresolved `GetConsoleMode`, `GetFileType`, and `WriteConsoleW` symbols. A plain full AMD64 build without proof feature defines also failed on conditional NativeAOT declarations. The full AMD64 kernel was then rebuilt with the repository proof defines and linked successfully; fresh `kernel.elf` SHA-256 is `97D919592E265D57BF5A6D19BE419E48CB9A40B9177A11CFCACB359F7FCF93F9`. The proof ramdisk was not produced because the composite build failed first.

QEMU discovery found one active `qemu-system-x86_64.exe` process (PID 27820). C166R7 guest proof was not run. C166 remains unaccepted pending the full host matrix, passing proof builds, and the previously defined guest-only FAT persistence/reboot, Explorer parity, clean production, and ordinary-boot gates. Settings remains v2; C128 remains unverified; no Default Apps UI was added.
