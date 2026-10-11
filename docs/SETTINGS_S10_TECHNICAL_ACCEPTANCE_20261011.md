# Settings S10 Technical and Acceptance Report

Date: 2026-10-11

## Outcome

**Outcome A — S10 accepted.** The hosted Settings Center consumes the shared
presentation state implementation, the shared component compiles and links
with the AMD64 production freestanding flags, the focused and hosted
regression suites pass, and the normal AMD64 kernel target builds. This is a
presentation-boundary result; it does not claim that Settings launches in a
native guest.

## Architecture and boundary

S10 uses `settings_presentation_core.h/.cpp` as a fixed-capacity C++14
presentation core. It owns the stable twelve-category identifiers, route
validation and transitions, top-level navigation, bounded case-insensitive
search, category-aware focus state, 41 declarative page/search descriptors,
capability presentation snapshots, and bounded request/completion status
transitions. It uses C headers only, does not allocate, and has no hosted
process, IPC, compositor, provider, or OS dependency.

`settings_center.cpp` remains the single hosted Settings application. It uses
the shared core for navigation, route and category state, search, focus
eligibility, and capability/operation presentation. The existing hosted
renderer, page-specific state, window lifecycle, provider calls, persistence,
and provider-authorized operations remain in their existing hosted owner
paths. `settings_center_model.h` now keeps only hosted compatibility helpers
and formatting/layout behavior; it no longer contains a second route/search/
focus implementation.

`settings_presentation_adapter.h` defines versioned transport-neutral value
and callback contracts for window constraints, input events, bounded provider
snapshot reads, capability snapshots, operation submit/result polling, and
presentation. Snapshot bytes are copied into caller-owned buffers with an
explicit size and revision; the core retains no runtime pointer. The contract
has no privileged identity field. Requests made by a Settings control remain
requests: trusted runtime/service authority assigns identity and authorizes
provider mutations, and provider results determine success.

The architecture note is `docs/SETTINGS_S10_PRESENTATION_ARCHITECTURE.md`.
`KernelApp`, `AppManager`, `KernelCompositor`, and the Native ELF ABI remain
future adapter targets. No native registration, syscall, service identity,
Start-menu remapping, or QEMU Settings launch was added. The repository's
.NET 9 project still invokes the C++ kernel build; the separate .NET support
checkout was not changed.

## Freestanding and build evidence

The AMD64 freestanding validator compiled the core and bounded harness with
the production `-std=c++14 -ffreestanding -nostdlib -nostdinc++
-fno-exceptions -fno-rtti` flags and architecture flags from the kernel
Makefiles. The core object was 17,767 bytes, the harness object 4,830 bytes,
and the relocatable linked object 21,635 bytes. The core object and combined
object have no unresolved symbols; the core symbol check found no hosted,
allocator, or C++ runtime symbols. The compile emitted no warnings.

The full hosted build completed successfully and produced the validation
executable under `out/validation/settings-s10-20261011/`. Its output included
existing warnings from unrelated hosted sources; a complete warning-line
count was not retained. The changed Settings/core sources compiled without
reported warnings.

The normal AMD64 kernel build completed with exit code 0 and zero error
markers. `kernel.elf` is 2,646,208 bytes and has SHA-256
`ED141D01B7C826B8E4C6DA1E834058DB2F3CF0EBF1075F31E49E615F3E783F8D`.
The fresh isolated build emitted 393 warning lines. The retained S9R log
contains the previously reported 80 warning lines from four compile commands.
This build used a new, empty output directory and performed 152 compile
commands across the existing kernel and vendored sources;
no kernel source or kernel build rule changed for S10, and the new Settings
core is not part of the kernel source set. The repeated current warnings are
in existing kernel headers/components and vendored sources, including
`kernel_app.h`, `virtio_gpu.cpp`, and `stb_image.h`; the separately compiled
Settings core has no warning diagnostics. The complete log and artifacts are
under `out/validation/settings-s10-kernel-20261011/`.

## Test evidence

The focused shared-core suite passed **2,645/2,645** checks. It covers all
twelve category IDs, valid and invalid routes, navigation and search bounds,
focus eligibility, descriptors, capability availability and status,
operation payload limits, stale completions, 250 provider refresh cycles,
1,000 deterministic category transitions, 100 core reset cycles, and 100
scoped construction/destruction lifetimes.

The current hosted Settings/provider acceptance set passed **464/464** checks:

| Suite | Result |
|---|---:|
| Settings Center | 89/89 |
| S6 Apps and Date & Time | 42/42 |
| S7 Accessibility, Developer, navigation/search | 58/58 |
| S8 Users, navigation, interaction/layout | 32/32 |
| Devices and Storage inventories | 45/45 |
| Network contract and transaction | 66/66 |
| System-service bridge | 78/78 |
| Block-device generation | 5/5 |
| Default Apps model and interaction | 48/48 |
| Compositor resize notification | 1/1 |
| **Total** | **464/464** |

The Default Apps suite additionally completed **100/100** lifecycle cycles.
The shared-core suite is additional coverage and is not included in the 464
hosted-check total. The S7 test build reported its existing unused-variable
warning; no regression suite failed.

## Readiness and next phase

S9 and Settings 1.0 remain unaccepted because no guest evidence shows the
twelve-page Settings Center opening and operating in QEMU. This was an
expected S10 boundary and does not block S10 acceptance. v0.3 readiness was
not advanced, no `.phase` marker was introduced, and no release candidate or
pre-existing `out/` item was changed.

Recommended exact S11 scope: add a distinct native `gxos.builtin.settings`
launch registration through the existing `AppManager`/`KernelApp` path while
keeping the legacy Control Panel route intact; have the trusted runtime assign
any Settings identity/capability (never accept it from UI arguments); and use
the S10 adapter contract to display one real, read-only System/About provider
snapshot. Prove native window creation, input, close, and reopen in an
isolated QEMU guest. Do not add Settings write operations or wire the other
eleven categories until this slice demonstrates provider-backed state and
runtime ownership.
