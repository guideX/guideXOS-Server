# REXX RX18 — ABBREV, CENTER, and CENTRE

RX18 implements three classic string built-ins in the shared REXX runtime. The IBM z/OS classic REXX descriptions are the semantic authority for [`ABBREV`](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-abbrev-abbreviation) and [`CENTER`/`CENTRE`](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-centercentre).

## Behavior

`ABBREV(information, info[, length])` returns the normal REXX string `1` when `info` is an exact, case-sensitive byte prefix of `information` and its byte length is at least `length`; otherwise it returns `0`. The omitted minimum defaults to the byte length of `info`. Required arguments must be supplied, the optional minimum must be a nonnegative whole number, and an empty abbreviation matches when the minimum is zero. Explicit lengths are compared, so NUL and high-bit bytes remain significant.

`CENTER(string, length[, pad])` and `CENTRE(string, length[, pad])` return exactly `length` bytes. The default pad is ASCII space (`20` hex), and an explicit pad must contain exactly one byte. The source is centered with the extra pad byte on the right; when truncating, the extra removed byte comes from the right. Empty source and zero result length are supported. CENTRE dispatches to the same function as CENTER.

All three functions preserve their arguments and numeric settings. Lengths are whole, nonnegative values; invalid/missing arguments use the existing built-in error path. General results obey the existing 16 MiB runtime string cap. RX16's 250-byte packed-literal restriction does not constrain runtime strings.

## Implementation

The shared built-in code uses the existing length-bearing string views, decimal conversion, bounded result allocation, and VM-owned result representation. ABBREV uses a bounded prefix comparison instead of C-string or locale APIs. CENTER calculates the source offset/padding from checked `size_t`-bounded lengths, preserves byte values, and allocates only the required result. The CENTER and CENTRE dispatch descriptors point to one implementation.

The code remains freestanding-compatible: no STL, hosted filesystem/OS calls, exceptions, RTTI, or unbounded allocation were introduced.

## Qualification

- RX18 direct suite: **99,315 assertions**, **14,633 dispatch cases**, **13,935 deterministic property cases**, fixed seed `0x52583138`; zero failures.
- Direct coverage includes IBM's documented examples, invalid and omitted arguments, empty strings/abbreviations, case sensitivity, unequal lengths, NUL/high-bit bytes and pads, default versus explicit space, odd/even padding and truncation, alias equivalence, long boundaries through 16 MiB, allocation failure, input preservation, and recovery.
- The independent property oracle checks prefix matching and centered source selection through byte positions rather than duplicating the production allocation algorithm.
- The RX18 portable REXX fixture passed as part of the 31/31 RX12 string-builtin fixture family. Hosted CLI regressions passed 22/22; inherited compatibility/resource fixture families passed 280/280.
- ASan and UBSan each built and ran 21 targets, including RX18; both matrices completed with zero failures and no sanitizer diagnostics. The differential corpus had 194 local expected-result cases in each run; Regina was unavailable, so cross-interpreter comparison is `NOT_RUN` and no compatibility percentage is claimed.
- Freestanding AMD64 compilation and linking passed for nine REXX C objects. Kernel ELF SHA-256: `76687c3289a7b60a186453c806c4c633b5f679b1e15e2e04f9b20e927a0c4d32`.
- Isolated QEMU RX18 guest passed ABBREV/CENTER/CENTRE, NUL/high-bit byte output, invalid-length handling, a subsequent successful call, and QMP shutdown. Networking was disabled and disposable media was used.

## Evidence

- Hosted qualification: `docs/rexx/evidence/rx18/hosted-qualification.log`
- Preflight and protected-artifact hashes: `docs/rexx/evidence/rx18/preflight.json`, `protected-artifacts-start.json`, and `protected-artifacts-final.json`
- Untracked-entry reconciliation: `docs/rexx/evidence/rx18/untracked-preservation-reconciliation.json`
- ASan and UBSan results: `docs/rexx/evidence/rx18/sanitizer-address-85337204a7b34db4aba2df519e0b0528/` and `sanitizer-undefined-2b83dd405c204d1983717afa75b00fbd/`
- Native link evidence: `docs/rexx/evidence/rx18/native-build/native-link-evidence-3dcebde9dc284c5c87684ac495725e92.json`
- QEMU guest transcript, screenshots, process censuses, and shutdown evidence: `docs/rexx/evidence/rx18/native-guest/run-feefc1aaf4044126a6f6a9ea6835cec4/`

The preflight JSON declares 63 initial untracked status entries but its captured array has 65 entries. The two extra entries are the RX18 evidence directory and RX18 direct-test source, both generated during this phase and included in the snapshot. The reconciliation record identifies these explicitly; all 63 RX17 baseline entries remain present. The 31 root `.obj`/`.pdb` artifact hashes and sizes match the preflight manifest.

## Compatibility limits and next phase

The implementation operates on the runtime's byte representation and does not transcode ASCII and EBCDIC. Regina was not installed or found in PATH. Runtime strings retain the 16 MiB cap.

A useful RX19 scope is the paired `DATE` and `TIME` built-ins after a kernel-native time source and explicit UTC/local-time rules are available. This keeps the family cohesive and portable; environment-specific `ADDRESS`, stream/data-stack functions, and IBM TSO/E-only functions should remain separate. If a clock dependency is unavailable, defer that family rather than claiming host-dependent semantics.
