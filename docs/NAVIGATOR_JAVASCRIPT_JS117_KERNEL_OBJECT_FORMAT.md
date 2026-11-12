# Navigator JavaScript JS117 Kernel Object Format

**Outcome B — `JS117_KERNEL_FORMAT_PARTIAL`**

- **Starting HEAD:** `a0c98d55d44cea3552b1eb186921d373e82ce479`
- **Branch:** `NAVIGATOR_JAVASCRIPT_SUPPORT`
- **Target:** AMD64, MinGW GCC 15.2.0 (`x86_64-w64-mingw32`)
- **Evidence root:** `E:\CodexArtifacts\guideXOSServer_NAVIGATOR_JS_SUPPORT\JS117\run-20261111-01`
- **Guest execution:** Not performed or claimed

## 1. Result and gates

JS117 compiled the accepted JS116 lexer, AST, and parser with the production
kernel target profile, audited their COFF sections and relocations, and linked
them through the kernel's PE/COFF linker and PE-to-ELF conversion commands. A
test-only anchor kept the three real frontend entry points reachable. The
resulting partial candidate has the expected ELF entry and two nonoverlapping
load segments.

The official `mingw32-make all` did not reach a complete kernel link. Two
MbedTLS configuration-check translation units fail their existing compile-time
configuration checks: `library/mbedtls_config.c` reports partial ECC curve
acceleration and missing ECDHE-RSA prerequisites; `tf-psa-crypto/core/
tf_psa_crypto_config.c` reports partial ECC curve acceleration. The MbedTLS
profile verifier itself passes. Neither configuration nor kernel build flags
were changed to suppress these checks. For diagnostic evidence only, the
actual production link and conversion recipes were then run with all available
source-matched kernel inputs, omitting only those two failed objects and adding
the frontend component and qualification anchor. This is not a complete or
accepted production kernel image.

| Gate | Result | Evidence |
| --- | --- | --- |
| K0 — pipeline identified | Pass | Existing kernel Makefiles, AMD64 architecture Makefile, linker script, and conversion recipe were inspected. |
| K1 — frontend format compatible | Pass for the tested frontend component | AMD64 COFF machine, section, symbol, alignment, and relocation audits matched ordinary kernel COFF conventions; no unresolved component symbols or runtime imports. |
| K2 — kernel link integration | Partial | Actual production linker recipe accepted and retained the anchor and frontend in a source-matched partial object list. Full `make all` is blocked before link by the two MbedTLS configuration units. |
| K3 — final format and layout | Partial | The exact production conversion produced an ELF64 ET_EXEC with entry `0x100000` and valid, disjoint PT_LOAD ranges from the partial source set. |
| K4 — integration readiness | Partial | Caller-owned storage and C ABI boundary are documented; parser stack use and guest memory budget remain unqualified. |

The highest verified result is a partial K3 layout/conversion qualification. It
does not meet Outcome A because a complete production kernel input set was not
linked. The candidate was not booted.

## 2. Starting state and JS116 baseline

The checkout began at the requested JS116 checkpoint on
`NAVIGATOR_JAVASCRIPT_SUPPORT`, with a clean tracked worktree and two local
commits ahead of `origin/NAVIGATOR_JAVASCRIPT_SUPPORT`. The live protected
`guideXOSServer.exe` was 8,398,274 bytes with SHA-256
`A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971`.
All 42 generated Navigator smoke fixtures matched the saved JS116 manifest
(458,372 bytes total), and `.phase` was absent. Existing `build/amd64` and ESP
artifacts were not used as candidate output and were left untouched.

JS116's source-level baseline remains the portable lexer, AST, and parser,
caller-owned bounded records, 111/111 JavaScript lanes and 17,656/17,656
assertions, and the established hosted aggregate of 767/774 with seven known
CSS failures. JS117 changed no production JavaScript source. The JS116 semantic
suites were therefore not rerun; JS117 evidence concerns target compilation,
linking, conversion, and static image structure.

## 3. Actual kernel build pipeline

The existing AMD64 build is a PE/COFF-first pipeline, not an ELF-first link:

1. Kernel C/C++ units compile to AMD64 PE/COFF objects with MinGW GCC; boot
   assembly uses NASM `-f win64`.
2. GNU ld links the kernel inputs as `kernel.pe` with
   `kernel/arch/amd64/linker.ld`, `-no-pie`, `--image-base=0x100000`, and the
   MinGW `libgcc.a` runtime archive.
3. GNU objcopy converts `pei-x86-64` to `elf64-x86-64`, placing `.boot` at
   `0x100000`, retaining the production section-address adjustments, and
   setting the ELF start address to `0x100000`.
4. The normal wrapper packages `kernel.elf`, `BOOTX64.EFI`, and the ramdisk on
   the ESP. JS117 did not run that wrapper because it writes the existing ESP.

Tool versions were MinGW GCC 15.2.0 targeting `x86_64-w64-mingw32`, GNU ld and
objcopy 2.46.0.20260210, and NASM's Win64 object format. The frontend used the
kernel's C++14 standard and AMD64 freestanding flags, including `-m64`,
`-mcmodel=large`, `-mno-red-zone`, disabled MMX/SSE, non-PIE code, no stack
probe/check/protector, `-ffreestanding`, `-fno-builtin`, `-nostdlib`,
`-nostdinc++`, `-fno-exceptions`, and `-fno-rtti`. Warning flags included
`-Wall -Wextra -Werror -pedantic`. Explicit target C++ scalar and placement-new
headers were supplied while implicit C++ header discovery was disabled.

The production linker script sets `_start`, page-aligns boot, text, read-only,
writable, constructor, and BSS sections, and discards `.pdata`, `.xdata`,
`.eh_frame`, and debug inputs. Production Makefiles and linker script were not
modified.

## 4. Frontend object and ABI audit

The actual shared `lexer.cpp`, `ast.cpp`, and `parser.cpp` compiled cleanly
under the kernel's C++14 target configuration. Their combined relocatable
component is `pe-x86-64`, machine `i386:x86-64`, with no undefined symbols. The
component contains `.text` (0x19ba0 bytes, 32-byte alignment), `.rdata`
(0x11e0, 32-byte alignment), `.data` (0x80, 32-byte alignment), compiler
`.pdata`/`.xdata`, `.rdata$zzz`, and eight small `.rdata$.refptr` COMDAT
records. `.text` is allocated, loaded, read-only code; `.rdata` and reference
records are allocated, loaded, read-only data; `.data` is allocated, loaded
writable data. `.bss` is empty. The anchor object itself has 0x350 bytes of
`.text` and zero `.data`/`.bss`. There is no TLS section, static
initialization marker, constructor requirement, exception/RTTI import,
allocation import, or unresolved dependency. The linker script discards the
frontend `.pdata` and `.xdata` records according to the existing kernel rule.

Relocations in the component are AMD64 `ADDR32NB` (183), `ADDR64` (24), and
`REL32` (321). These are also the relocation families in an ordinary kernel
object (`kernel_apps.o`). Component symbols use the normal C++ mangling and
AMD64 calling convention internally. The qualification boundary is the
explicit C symbol `gxos_js117_frontend_link_anchor`; it takes raw pointers,
capacities, and a source length, then calls the shared lexer, AST, and parser.
The 64-bit Windows AMD64 target supplies pointer representation, default
calling convention, stack alignment, and callee-save rules; there is no
handwritten assembly bridge. The anchor's exported C name is undecorated while
frontend calls retain the target's ordinary C++ mangling. Its record buffers
are aligned to their compiled target types (at most 8-byte alignment in this
frontend); the arena performs aligned allocations from caller storage. The
anchor checks pointer presence and token/node/child storage alignment, but
owns no large arrays and has no global writable or BSS state. The target
compiler generates the stack frames; no independent guest ABI/runtime probe
was run. The anchor is not in the production source list and is not called at
startup.

The link map retains `Lexer::tokenizeInto` at `0x24fea0`, `Ast::addNode` at
`0x254750`, `Parser::parseInto` at `0x267210`, and the anchor at `0x268c80`.
Required frontend read-only and writable sections remain in the image; the
candidate has 128 bytes of `.data`, and the frontend adds no `.bss`. The map
shows one frontend component and one anchor, with no duplicate frontend
implementation.

## 5. Link, conversion, and image layout

The partial baseline and candidate used the actual production link recipe and
the exact PE-to-ELF conversion command. A strict candidate link with
`--no-undefined` also succeeded for that same partial input list. This proves
resolution for the included objects only; it does not repair or qualify the
two missing MbedTLS objects.

| Artifact | Size | SHA-256 |
| --- | ---: | --- |
| Partial baseline `kernel.pe` | 2,601,376 | `36E1BE831444D2D2FB19481CD90BBEE6E9A4103736806D6F20D3FB48DF44CCE8` |
| Partial baseline `kernel.elf` | 2,511,056 | `0F3F0B32184A3E5B5829AECFC7BF621D602EA0A3CA9D647E1C0F7CC2D00CB969` |
| JS117 candidate `kernel.pe` | 2,722,845 | `DA2CB43B61865E2D39CDD20290F9D3ABE7D62ADD0D6E52D0F63E97666E4AB0E7` |
| JS117 candidate `kernel.elf` | 2,629,248 | `C00E4DC898C8FB74880E581C741824856CFB7E90F5D143322B944C9AE4457D01` |

Candidate growth over the partial baseline is 121,469 bytes in PE and 118,192
bytes in ELF. PE section sizes changed by `.text` +0x19ec0, `.rdata` +0x13c0,
`.data` +0x90, and `.reloc` +0x3c. `.bss` remained 0x4521a60; link-map page
alignment contributes to the image growth. This size comparison isolates the
frontend and anchor against the same partial base, not against a complete
production kernel.

The converted candidate is ELF64 little-endian ET_EXEC for AMD64 with entry
`0x100000`. `_start` and `__kernel_start` are at that address; `kernel_main` is
at `0x1e0930`. The PE optional header's entry RVA is zero because the entry
address equals its `0x100000` image base; the production conversion explicitly
sets ELF `e_entry` to `0x100000`. No unresolved final relocation records remain
in the ELF.

The converted section and segment layout is:

- `.boot`: `0x100000`, 0x25 bytes; `.text`: `0x101000`; `.rdata`: `0x26f000`;
  `.data`: `0x313000`; `.ctors`: `0x323000`; `.bss`: `0x324000` through
  `0x4845a60`.
- PT_LOAD 1: virtual/physical `0x100000`, file and memory size `0x212030`,
  read/execute, 0x1000 alignment; end `0x312030`.
- PT_LOAD 2: virtual/physical `0x313000`, file size `0x10048`, memory size
  `0x4532a60`, read/write, 0x1000 alignment; end `0x4845a60`.
- The segments do not overlap; the inter-segment gap is 0xfd0. `.reloc` is at
  `0x4846000`, outside the load segments, following the production conversion
  rules.

The boot stack remains in BSS with `stack_top` at `0x4843000` and the existing
16 KiB reservation below it. The boot entry's stack relocation resolves into
that range, and startup instructions and boot data remain in their existing
sections. These are static layout checks only; no loader or guest consumed this
image.

## 6. Storage, stack, and future runtime boundary

The C ABI anchor accepts caller-owned source, token, AST node, AST child, and
parser scratch buffers. It calls the existing bounded frontend APIs and returns
the actual lexer/parser status. It does not use heap allocation, global
storage, startup hooks, or an invented success path. This establishes symbol
reachability and a possible narrow invocation boundary, not execution safety.

JS116's largest token/AST/child/scratch backing estimate is 3,145,792 bytes,
excluding a maximum 1 MiB source buffer. The heap is currently 32 MiB with
8-byte alignment; current frontend records require at most 8-byte alignment.
Allocating or initializing these buffers at boot is out of scope. Under this
JS117 C++14 compile, `.su` frame reports show 608 bytes for the anchor, 688 for
`Parser::parseInto`, and 1,344 for `parseAssignmentExpression`. JS116's
approximately 96 KiB parser stack estimate was configuration/compiler-derived,
not a runtime measurement, and is not replaced by the changed frame figure.
The kernel boot stack is only 16 KiB. A later invocation must first establish
an adequate caller stack or bounded execution strategy and validate the actual
storage budget in the guest.

## 7. Negative qualification checks

Controlled negative checks were run against safe object/layout probes. No
malformed image was booted, and invalid candidates were not executed.

| Check | Result |
| --- | --- |
| Wrong object format | Unrecognized bytes rejected. An empty same-architecture ELF relocatable object can be tolerated by GNU ld, so the production pre-link COFF format audit is required. |
| Wrong architecture | Win32/i386 COFF rejected when linking to AMD64 COFF. |
| Missing frontend symbol | Omitting parser object fails on `Parser::parseInto`. |
| Unresolved dependency | Strict link rejects a deliberately unresolved C probe. |
| Unexpected standard library | `std::string` probe exposes allocation, throw, and unwind imports; audit rejects them. |
| Exception runtime | try/catch probe exposes `__cxa_*`, RTTI, and personality dependencies; audit rejects them. |
| Unsupported relocation | Corrupted AMD64 relocation survives `ld -r` but final link rejects illegal relocation type 65535. |
| Discarded frontend section | Removing frontend `.text` makes the anchor link fail on lexer, AST, and parser references. |
| Zero entry | Existing JS114 diagnostic ELF with entry zero was inspected and rejected; it was not used as a kernel image. |
| Invalid section alignment | ELF ET_REL probe reports alignment 3, which is not zero, one, or a power of two. |
| Image overlap | Candidate load ranges are disjoint; a synthetic overlapping pair is detected without emitting an image. |
| Unsupported conversion | objcopy rejects an unknown output format and produces no output. |
| Missing kernel input | Make rejects a missing prerequisite before link and emits no kernel. |

Individual commands and output are retained in the evidence root's `logs/` and
`negative/` directories, summarized by `metadata/negative-tests.json`.

## 8. Provenance, preservation, and regressions

`metadata/candidate-provenance.json` records the starting HEAD, source hashes,
compiler and flags, frontend object hashes, kernel input hashes, linker script
hash, linker and conversion commands, output hashes, entry, and PT_LOAD
summaries. It explicitly lists the two compile-blocked source files and labels
the candidate's input set partial. The link map is `candidate/kernel.map`.

The original hosted executable remains at its historical size and SHA-256. All
42 generated fixtures match the saved JS116 manifest, totaling 458,372 bytes,
and `.phase` remains absent. Candidate images, maps, logs, and provenance are
outside Git and separate from the existing build and ESP artifacts.

No JavaScript or production kernel source changed, so JS115/JS116 semantic
regressions and the hosted production build were not rerun. Their baseline
results remain historical JS116 evidence, not new JS117 execution evidence.
No QEMU launch or QAC reservation occurred.

## 9. Remaining work and recommended JS118 scope

Before Outcome A can be claimed, restore the full production kernel build by
resolving the MbedTLS configuration-check failures through their intended
configuration and dependency contract, then rerun the official kernel build
and source-matched conversion with every required input. Do not suppress the
checks or use this partial candidate as a release image.

JS118 should first close that full-build prerequisite. Then it can define and
measure the guest caller's stack and buffer ownership, integrate the shared
frontend behind a bounded Navigator invocation boundary without startup
execution, and qualify runtime behavior under the appropriate later guest
test phase. Runtime values, JSON/DOM bindings, NativeElf loading, EFI packaging,
and QEMU/QAC remain outside JS117.
