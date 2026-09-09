# AARCH64 Phase 10 — Multi-architecture NativeElf packages

Status: implemented on `AARCH64_SUPPORT`.

Phase 10 completes the transition from a single-architecture NativeElf
package to one logical App Model package with native payload variants. The
package identity and `app.json` are shared; only the selected ELF changes.

## Previous package model

Before Phase 10, the hosted App Model already had architecture-tagged
`entries`, but the ARM64 bare-metal bridge selected an ARM64 entry with a
private parser and the package resolver did not provide one common policy.
The first ARM64 package therefore looked like:

```text
/Apps/Phase8Arm64GuiProof/app.json
/Apps/Phase8Arm64GuiProof/bin/arm64/phase8-arm64-gui-proof.elf
```

Phase 10 keeps that representation valid and adds the common
`AppPayloadResolver`. An optional manifest `executable` field is also
supported when a package has no `entries`; it expands to
`bin/<architecture>/<executable>`.

## Canonical architecture contract

`gxos::apps::NativeArchitecture` is the single native payload identity used
by common package code:

```text
NativeArchitecture::AMD64 -> amd64 -> EM_X86_64 (62)
NativeArchitecture::ARM64 -> arm64 -> EM_AARCH64 (183)
```

`CurrentNativeArchitecture()` is implemented in the common architecture
boundary and consumes target/compiler architecture information. Package code
does not inspect ARM registers or x86 CPUID state. Compatibility parsing
accepts `x64`, `x86_64`, and `aarch64`, but all internal and emitted values are
canonical `amd64` or `arm64`.

## Package and manifest design

The proof package is:

```text
/Apps/Phase10MultiArchProof/
    app.json
    bin/
        amd64/
            phase10-multiarch-proof.elf
        arm64/
            phase10-multiarch-proof.elf
```

It has one identity, `com.guidexos.phase10.multiarchproof`, one manifest, and
two `entries` that use the existing schema. `entries` is intentionally kept
for backward compatibility and carries the entry point, ABI, runtime, and
architecture-specific path. Shared resources remain beside `bin`, not inside
architecture directories.

For the optional compact form:

```json
{
  "kind": "NativeElf",
  "executable": "app.elf",
  "supportedArchitectures": ["amd64", "arm64"],
  "entries": []
}
```

the resolver selects `bin/amd64/app.elf` or `bin/arm64/app.elf`.

## Resolver policy

`AppPayloadResolver::Resolve` owns the architecture-neutral selection policy:

1. Reject an unknown current architecture.
2. Require the current architecture in `supportedArchitectures` unless the
   manifest uses an explicit wildcard.
3. Prefer one exact architecture entry.
4. Accept one legacy wildcard (`any` or `*`) only when no exact entry exists.
5. If there are no entries, infer `bin/<canonical-architecture>/<executable>`.
6. Validate the path, canonicalize it, require a regular file, and verify it
   remains below the package root.

Duplicate exact architecture declarations, absolute paths, drive-qualified
paths, backslashes, duplicate separators, `.`/`..` components, control bytes,
oversized identifiers, and traversal are rejected deterministically.

An ARM64 package with only an AMD64 payload returns
`APP_ARCHITECTURE_NOT_AVAILABLE`; it never falls back to the AMD64 file.
An unknown architecture returns `UNSUPPORTED_ARCHITECTURE` and never defaults
to AMD64. Legacy explicit paths such as `bin/amd64/foo.elf` continue to work
when evaluated for AMD64.

Selection is followed by the existing NativeElf defense-in-depth validator.
The ARM64 backend still requires ELF64, little-endian, ET_EXEC,
`EM_AARCH64`, valid program-header bounds, non-overlapping load ranges, no
W+X segment, and an executable entry point. The AMD64 backend retains its
existing `EM_X86_64` checks; host controls validate both selected files.

## Proof application and artifacts

Both payloads are independently compiled from:

```text
sdk/samples/phase10_multiarch_proof/main.cpp
```

The application reports its compile-time selected architecture from the
running payload, creates a normal GUI window through the Phase-9 GUI ABI,
waits through the Phase-9 event service, handles a real button event, closes,
returns `42`, and is relaunched through the same application ID.

The build output records exact entry points, image layouts, sizes, and SHA-256
values in:

```text
out/aarch64-phase10/phase10-arm64-llvm-readobj.txt
out/aarch64-phase10/phase10-amd64-llvm-readobj.txt
```

The build script independently verifies `EM_AARCH64` and `EM_X86_64` before
staging both files under one package.

## Automatic selection and guest proof

The Phase-10 task requests only:

```text
com.guidexos.phase10.multiarchproof
```

The common ARM64 package table selects and logs:

```text
[guideXOS] package architecture: arm64
[guideXOS] selected payload: bin/arm64/phase10-multiarch-proof.elf
[guideXOS] selected ELF machine: EM_AARCH64
```

The application-originated markers prove package identity, running
architecture, GUI ABI, payload selection, button delivery, and normal close.
The existing kernel-owned Phase-9 runtime supplies handles, quotas, cleanup,
event wait/wake, and resource accounting. The same package is launched 26
times per fresh boot (one interactive launch plus 25 automatic relaunches),
with no allocator/resource delta.

Three fresh boots use independent UEFI variable files. The acceptance script
also checks one discovery entry, no wrong-machine execution, stable selection,
wait/wake, relaunch, and absence of unexpected IRQs or exceptions.

## Host/common proof and negative controls

`tests/aarch64_phase10_host_tests.cpp` proves:

* ARM64 resolves exactly `bin/arm64/proof.elf` and validates `EM_AARCH64`;
* AMD64 resolves exactly `bin/amd64/proof.elf` and validates `EM_X86_64`;
* 100 repeated ARM64 resolutions are stable;
* missing architecture never falls back;
* unknown architecture is deterministic;
* a wrong machine in the correct directory is rejected by ELF validation;
* traversal, malformed, oversized, and duplicate declarations fail closed;
* legacy explicit entries remain valid;
* canonical `executable` inference remains below the package root; and
* one package directory produces one App Model discovery entry.

## Build and test

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-aarch64-phase10.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-aarch64-phase10.ps1
```

The build compiles both payloads and the ARM64 kernel. The test performs the
common controls and three fresh interactive QEMU boots. Phase 9 regression is
run from its existing acceptance script. Phase 8, Phase 5, and historical
Phase 6 results remain separate regression entry points; Phase 7's bounded
high-level input result remains valid, while the documented high-rate QMP
gate remains externally limited and does not become `AARCH64_PHASE7_PASS`.

## Updates and remaining limitations

No installer or cache was added. A future package update must invalidate the
registered manifest generation and any payload-resolution result keyed by
package generation, manifest generation, and canonical architecture.
The current ARM64 proof still uses the Phase-9 kernel-owned runtime and
identity address space; EL0 isolation, SMP, GPU acceleration, and a high-rate
QMP transport redesign remain out of scope. Full AMD64 bare-metal boot is not
claimed where the existing Mbed TLS environment blocks it; the genuine AMD64
ELF, ABI layout, resolver, and machine checks are covered here.

## AARCH64-11 recommendation

Implement package-generation-aware installation/update invalidation and move
the common payload resolver behind the production package manager boundary,
then add a real AMD64 guideXOS boot proof. Keep the canonical architecture
enum and no-fallback rule unchanged while adding the next architecture only
after its NativeElf ABI and backend validation are independently complete.
