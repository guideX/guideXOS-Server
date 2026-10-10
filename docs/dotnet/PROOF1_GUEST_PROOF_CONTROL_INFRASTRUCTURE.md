# PROOF1 — Reusable Guest Proof-Control Infrastructure

Date: 2026-10-10
Branch: `v1.1_DOTNET_SUPPORT`
Prompt phase: `PROOF1`

## Starting state

The checkout was on `v1.1_DOTNET_SUPPORT`, tracking `origin/v1.1_DOTNET_SUPPORT`,
at `a4e216f905b5e42cb130c6da0b5b5e1b6bbf5048`. The prompt expected
`f2f0297ba7b323c9735d3828c5b0520fa9f86fda`; the actual HEAD was newer and
contained the committed C166R11/R12 audit. The worktree was clean. The audit
document was left intact; this document and a short pointer were added after
its R12 history.

## Architecture and isolation

The proof path is deliberately fixed and opt-in:

```text
boot ramdisk selector
  -> proof-build-only startup launch
  -> managed scenario coordinator
  -> fixed C166 operation sequence
  -> GuideXosFileAssociations
  -> ABI v4 associationService callback
  -> native association authority
```

`GUIDEXOS_PROOF_CONTROL` is the one general gate. The NativeAOT build helper
passes it as `-ProofControl`; MSBuild defines the matching C# symbol, and the
kernel proof build passes the same spelling as a C/C++ define. The managed
selector reader and coordinator, proof-only Diagnostics wrapper method, and
proof startup launch are conditional on that symbol. Without it, managed
dispatch does not read proof metadata, the coordinator is absent, and native
operation 4 is rejected. Normal builds do not expose an RPC endpoint, shell,
or arbitrary command parser.

The only registered handler is C166. Registration is a fixed one-element
managed array with a bounded lookup; historical Notes and Task Manager suites
are not migrated in this phase. The handler supports Query, SetManagedNotes,
Disable, Reset, and Diagnostics. Mutating steps call the actual managed C166
wrapper, which dispatches through the existing v4 host callback.

## Selector and startup

The selector is a fixed nine-byte record stored at
`/system/config/proof1.bin` in the boot ramdisk, not on the product FAT disk:

| Byte(s) | Meaning |
| --- | --- |
| 0–3 | ASCII `GXPF` |
| 4 | Record version `1` |
| 5 | Suite: `1` = C166 |
| 6 | Scenario: `1` = SameBoot, `2` = Persistence |
| 7 | Stage: `1` = SameBoot, `2` = PreReboot, `3` = PostReboot |
| 8 | XOR of bytes 4–7 |

The parser rejects wrong length, magic, version, checksum, suite, scenario,
and stage. In a proof kernel, startup launches the managed Notes entry once;
the managed dispatcher checks for the selector before ordinary app dispatch.
The scenario actions run automatically and do not depend on GUI focus or
keyboard ownership. With no selector file, managed execution continues with
its normal application behavior.

## Coordinator, actions, and serial protocol

The coordinator has fixed action sequences:

| Selection | Numbered actions |
| --- | --- |
| C166 / SameBoot / SameBoot | 1 Query, 2 Disable, 3 Query, 4 Reset, 5 Query |
| C166 / Persistence / PreReboot | 1 Query, 2 Disable, 3 Diagnostics |
| C166 / Persistence / PostReboot | 1 Query, 2 Diagnostics, 3 Reset, 4 Query |

Every new guest process starts numbering at 1. The first failing operation
stops the sequence. The fixed result buffer is 96 bytes; compact step records
have the form:

```text
PROOF C166 P Pre 3 PASS Diag st=0 ov=2 slot=A gen=1
```

Scenario tokens are `S` (SameBoot) and `P` (Persistence); stage tokens are
`Same`, `Pre`, and `Post`. Slot tokens are `A`, `B`, and `N` (no active slot).
The operation and status fields are bounded tokens and integers. Each valid
scenario emits one final marker:

```text
PROOF-SCENARIO-PASS C166 P Post
PROOF-SCENARIO-FAIL C166 P Post
```

An unknown selector is rejected and logs a bounded configuration failure.
Host validation checks the exact ordered step count and exactly one completion
marker rather than treating silence or QEMU exit as completion.

## Cross-boot runner and storage identity

The C166 runner builds separate SameBoot, PreReboot, and PostReboot ramdisks;
each carries its own selector record. For guest execution it stages the kernel,
bootloader, and selected `ramdisk.img` in one shared ESP directory and boots
that same `fat:rw` directory as IDE device 0 for all three fresh QEMU
processes. Boot 2 and Boot 3 replace only `ramdisk.img`; the writable root FAT
directory containing `/GXAS0.BIN` and `/GXAS1.BIN` remains at the same absolute
path. The runner asserts this identity before comparing the two stages.

After Boot 2, the host writes a run-specific orchestration record under
`out/dotnet/proof1/guest-run-*/c166-persistence-state.json` containing the
expected slot, generation, Boot 2 serial SHA-256, and backing disk path. Boot 3
must report the same slot and generation before Reset. That file is host test
metadata only and is never included in the guest VFS image.

QEMU is launched hidden with a private QMP port. The runner sends QMP `quit`
after observing the final marker, and its fallback termination is restricted
to the process handle it launched. Existing QEMU processes block the guest
lane and are never stopped. Because the managed coordinator starts at boot,
QMP input events are unnecessary for the fixed action scripts.

## Diagnostics and ABI

Diagnostics is operation `4` on the existing v4 `associationService` callback.
It returns authoritative active slot, generation, and override state. The
operation has no persistence branch and does not change the active slot or
generation. The native host test snapshots both values around Diagnostics;
the managed fake-callback test verifies routing and response layout. The
host-call table remains 128 bytes and callback offset remains 120. The
association response record grows from 316 to 328 bytes for the appended
diagnostic fields; the managed and native layouts are asserted together.

Diagnostics is accepted only in `GUIDEXOS_PROOF_CONTROL` builds. Production
native service builds reject operation 4, and ordinary managed builds contain
no proof coordinator or selector reader.

## Verification and remaining acceptance work

The C166 native and managed host suites passed, including the mocked managed
coordinator route `Query -> Disable -> Query -> Reset -> Query`, selector
validation, bounded registration, step sequence, duplicate completion gate,
marker size, and diagnostics read-only checks. The proof-disabled managed
production configuration compiled successfully. The proof NativeAOT composite
and all three selector ramdisks built from the working source.

The configured AMD64 ELF compiler tools (`x86_64-elf-gcc`, `g++`, `ld`, and
`objcopy`) were not found on PATH or in the repository's known
`C:\x86_64-elf-tools\bin` location. Repository-aware discovery found the
Makefile's configured MinGW fallback at `C:\mingw64\bin`; it built the proof
kernel successfully. The NativeAOT composite, all three selector ramdisks, and
proof kernel therefore have a build path. QEMU 11.0.0 is installed, but the
three guest scenario boots were not run in this phase. C166 remains
host-validated and guest-unaccepted pending those boots.

Settings remains format v2. C128 remains unverified. No Default Apps UI was
added, file-association algorithms were not redesigned, and the native C166
stress hook is not used as managed-ABI evidence.

The phase manifest is `out/dotnet/proof1/proof1-manifest.json`. It records the
source HEAD, build gate, registered suites, scenarios and stages, marker bound,
test and build outcomes, QEMU availability, and guest-lane status.
