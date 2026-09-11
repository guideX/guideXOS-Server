# AARCH64 Phase 12 — Developer Studio SDK convergence and in-OS IDE proof

**Status:** Complete on `AARCH64_SUPPORT` when the Phase 12 build and three
fresh QEMU boots report `AARCH64_PHASE12_PASS`.

Phase 12 closes the SDK skew between the server and the standalone Developer
Studio. The server SDK is the canonical ABI source. The Studio builds against
that SDK for both hosted AMD64 and freestanding ARM64, and the normal App Model
launch path loads the ARM64 Developer Studio NativeElf from `/Apps`.

## Canonical SDK and compatibility gates

The evolution rule is append-only: old fields, callback slots, enum values, and
sizes remain stable; new consumers read an appended field only when the
advertised structure size reaches that field.

| Surface | Legacy v1 gate | Current v2 layout | Compatibility rule |
| --- | ---: | ---: | --- |
| `gx_development_run_request` | 72 bytes | 104 bytes | v1 callers are accepted; v2 adds artifact size/architecture/ABI and capability bits |
| `gx_development_run_snapshot` | 448 bytes | 4560 bytes | v1 callers receive the legacy prefix; v2 adds bounded output and capabilities |
| output capture | unavailable | 16 lines × 256 bytes | `outputCount` is bounded; `outputTruncated` is sticky when capacity is exceeded |
| host callback table | `GX_GUI_V1_HOST_CALLS_SIZE` | full table 400 bytes | run callbacks are appended at offsets 360, 368, 376, 384, and 392 |
| bare-metal callbacks | absent | appended after file/build callbacks | exposed only by the Phase 12 NativeElf runtime |

The native ABI layout test asserts the offsets and sizes above. The ARM64
runtime and hosted runtime accept both request versions and both snapshot
capacities. A v1 request cannot accidentally read v2 pointers, and a v1
snapshot preserves `version == 1` when the caller supplies the 448-byte v1
capacity.

Capability bits are independent of the API version:

- `GX_DEVELOPMENT_RUN_CAP_ARTIFACT_METADATA` — artifact identity was validated
  by the producer;
- `GX_DEVELOPMENT_RUN_CAP_OUTPUT_CAPTURE` — bounded run output is available;
- `GX_DEVELOPMENT_RUN_CAP_DEBUG_DIAGNOSTICS` — debug/lifecycle diagnostics are
  available.

The hosted service advertises artifact metadata and debug diagnostics. The
bare-metal service advertises all three because it captures the nested NativeElf
output into the bounded snapshot.

## Field ownership, lifetime, and truncation

The request is producer-owned by the Studio until `prepare` returns. Pointer
fields are borrowed for the call; the service copies only validated scalar
metadata and does not retain caller pointers. `artifactSize`,
`artifactArchitecture`, and `artifactAbi` describe the already-built artifact
and are consumed by the validator.

The snapshot is producer-written and consumer-owned after each successful
`poll`. The consumer must provide `size` and `version` before the call and must
read only fields covered by that size. Strings and output lines remain valid
until the next poll or release. Output has a fixed capacity of 16 lines and
256 bytes per line; excess lines are dropped and set `outputTruncated`, while
the retained prefix remains readable.

`capabilities` is producer-owned metadata. `reservedOutputAlignment` and
`reservedV2` are reserved and must be ignored and preserved as zero by new
callers.

## CALL_DEPTH and negative controls

`GX_DEVELOPMENT_RUN_ERROR_CALL_DEPTH_EXCEEDED` is the explicit bounded-runtime
result for recursive/nested run entry beyond the runtime’s permitted call
depth. It is distinct from `RUNTIME_BUSY`: a second unrelated deployment is
busy, while an owned nested call that exceeds the depth budget is rejected.
Pointer diagnostics are similarly explicit: an invalid dereference and an
out-of-bounds pointer are reported as `INVALID_POINTER_DEREFERENCE` and
`POINTER_OUT_OF_BOUNDS`; they are not collapsed into generic launch failure.

The negative-control contract is observable in the service tests and runtime
guards:

- stale, zero, duplicate, or owner-mismatched handles are invalid;
- releasing a running deployment is rejected until cleanup completes;
- a close request is idempotently gated by the state machine and unsupported
  when the bare-metal backend has no close operation to perform;
- v1-sized requests/snapshots never consume v2-only fields;
- changed artifact size/hash/architecture/ABI and missing entry points reject
  before launch;
- output capacity and pointer/size checks reject malformed buffers without
  mutating beyond the advertised structure size.

Release is valid only after a terminal snapshot (`COMPLETED`, `FAILED`, or
`CANCELLED`) and after the owner’s cleanup has run. A successful release clears
the generation slot; any later use of that handle is stale by construction.

## In-OS IDE workflow

The Phase 12 proof project is staged at `/Phase12IDEProof` with the normal
project contract: `guidexos.project`, `CMakeLists.txt`, `build.ps1`, `README.md`,
`app/app.json`, and `src/main.c`. Its logical target profile is
`guidexos.multi.baremetal.bootstrap.native`. Its canonical published package
root is `/Apps/P12Proof`; the short root is required because the guest FAT
create path is 8.3-only, while the package manifest remains the standard
`app.json`.

On a fresh ARM64 boot, App Model discovers and launches the ARM64 Developer
Studio ELF. The Studio creates its real window through the App Model callback,
opens the proof project and source document, edits the source, and drives the
same workspace, build, output, and run controllers used by the interactive IDE.
The resident compiler then:

1. compiles one source into genuine `EM_AARCH64` and `EM_X86_64` ELFs;
2. validates and publishes both entries into the canonical package;
3. rebuilds invalid source and reports a source diagnostic while retaining the
   previous package;
4. rebuilds valid `build=2` source with a newer package generation and changed
   artifact digests; and
5. refreshes App Model and runs the ARM64 payload, which returns 42 and emits
   the expected captured output.

The ARM64 Studio image is linked at `0x50000000`, matching the NativeElf loader
contract. Its freestanding profile is intentionally bounded; it supports the
bootstrap C subset used by the proof rather than claiming a general hosted
C/C++ toolchain. The hosted Studio remains the richer multi-file model.

No host process compiles the user target. Before the guest build, the Phase 12
staging check asserts that the proof project has no `bin` target output. The
only pre-created package artifact is the manifest skeleton needed for the
guest 8.3 overwrite path; target ELFs appear only after the resident compiler
has emitted them inside guideXOS.

## Validation commands

From the server repository:

```powershell
.\scripts\build-aarch64-phase12.ps1
.\scripts\test-aarch64-phase12.ps1 -SkipBuild -Boots 3
```

The first command runs the host ABI/layout controls, builds the ARM64 kernel
and Studio, stages the image, and checks pre-guest target absence. The second
uses fresh UEFI variable copies and requires the full marker set on every boot,
including `AARCH64_PHASE12_PASS` and the absence of
`AARCH64_PHASE12_ERROR`.
