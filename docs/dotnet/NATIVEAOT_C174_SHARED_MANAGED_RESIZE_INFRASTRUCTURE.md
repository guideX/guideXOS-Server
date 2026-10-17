# NativeAOT C174 Shared Managed Window Resize Infrastructure

**Repository:** `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`
**Branch:** `v1.1_DOTNET_SUPPORT`
**Starting HEAD:** `5c62dae54df7b015bef9e0d92190227353a65160`
**Outcome:** A — Accepted.

## 1. Preflight and accepted baseline

The repository, branch, and starting HEAD matched the C174 request. Before
changes, the only worktree modification was the inherited `ESP/ramdisk.img`;
its SHA-256 was `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`.
No `.phase` files were present. The acceptance scripts use source-head and
per-file hash checks instead: C172/C173 manifests must match the current HEAD,
and production evidence rechecks the source files recorded by focused proof.
C174 extends those inventories for its shared helper and tests. The C166 proof
products and the reused focused C169 UI inputs were verified against the current
source inventory. The proof-disabled production composite, kernel, and ramdisk
were rebuilt from the C174 source before acceptance.

C173R1's tracked acceptance record reports Outcome A / PASS. Its referenced
ordinary production manifest, serial log, QMP log, and dual-window screenshot
were present and matched their recorded SHA-256 values:

| Artifact | SHA-256 |
| --- | --- |
| Supplemental production run manifest | `087B2CC4EE0E09F414B3FA31594952E245DD0F271DF29EE97A5978CB58507332` |
| Ordinary guest serial | `D00516F939A0F0BA476BDEC4D75B81EDD5DBA6AB0862543A94E83E42552A9151` |
| QMP input | `DCB5C255E4EC3293E3B4F70F5C35D7DE9F76C4A0BABCA9240F8E801EE898186D` |
| Dual-window production screenshot | `01CA16443B8556A1CBF1B7CA7748DDB56FB30D31076086FC9F292E4300C51CAA` |

The C173R1 run opened Settings and Explorer together, resized each while the
other stayed open, rejected unsupported geometry, retained Explorer's
`hello.txt` selection, restored focus, and activated Notes. That evidence is a
baseline only; it does not replace C174 guest qualification.

## 2. Existing architecture and duplication audit

C171 established Settings layout calculation and retained its controls,
scrolling, and state when geometry changed. C172 added live resizing through
the compositor's existing resize drag, native owner callback, existing managed
action channel, and Settings selector. C173 routed that same action to the
existing Explorer instance. Native code still owns outer-window rectangles,
clipping, input capture, and rejection of the compositor proposal. Applications
own their layout, bounds, and UI state.

| Responsibility | Before C174 | Finding and C174 decision |
| --- | --- | --- |
| Geometry action encoding | One shared C172 encoder/decoder used by both apps | Already shared; kept unchanged. |
| Outer bounds | Settings and Explorer each validate their own minimum and maximum | Keep limits in each app; feed them to one shared inclusive validator. |
| Client dimensions | Both resize paths subtract the 24-pixel titlebar | Move this invariant into the shared policy. |
| Layout calculation | Settings lays out pages, groups, controls, and scroll view; Explorer lays out its list, details, buttons, and scrollbar | Application-specific; keep in each app. |
| Resize acceptance | Both perform layout, render, state checks, and result mapping separately | Share a small transaction value type and result mapping. |
| Rollback state | Settings needs focus/member and scroll offset; Explorer needs path, selected entry/index, focus, and list offset | Keep snapshots and restoration with each app; share prior outer geometry and rollback decision. |
| Hot-path storage | Fixed controls and buffers; previous stress runs reported zero resize allocation | Use structs and no delegates/callbacks; retain zero-allocation target. |

The C173R1 concurrency proof showed the two application instances already had
independent geometry and identities. A new window manager, event path, or
inheritance layer was not needed.

## 3. Shared facilities and app ownership

`GuideXosManagedWindowResizeBoundsC174` centralizes the 24-pixel native titlebar
and validates inclusive outer bounds. It creates a validated absolute candidate
with outer and client sizes. Settings supplies its existing 512×222 through
720×420 bounds; Explorer supplies 600×360 through 960×526. Both keep their
existing default sizes and layout calculations.

`GuideXosManagedWindowResizeTransactionC174` records the prior size, candidate,
failure stage, and `GuideXosResult`. Both resize handlers use the same mapping:
invalid geometry, layout rejection, and failed state invariants return
`InvalidArgument`; a render failure preserves the renderer's result. A rejected
completed attempt requests rollback even when its requested size equals the
prior size, since app layout code may already have changed controls.

The application still performs rollback with its own snapshot. Settings restores
its old layout, scroll offset, focus control/member, and surface rendering.
Explorer restores its old layout and list offset, while retaining its path and
selection. Layout failures are now routed through this rollback path as well.
The controller and Settings layout accept the already validated candidate, so
they do not repeat outer-bound validation in the resize handler.

The shared path uses value types and no captured delegates. The settings action
selector, Explorer action selector, compositor callback, and geometry action
encoding remain unchanged.

## 4. Host regression evidence

The consolidated managed UI gate passed **24 suites / 736 cases**. The previous
C173 inventory was **22 suites / 710 cases**; C174 adds two suites and 26 cases
(22 focused transaction cases and four stress assertions). No previous suite
was removed.

| Coverage | Result |
| --- | --- |
| C172 Settings resize | 43 focused cases and 7 stress checks passed; 1,000 geometry updates, 500 alternating transitions, zero allocation, zero drift. |
| C173 Explorer resize | 47 focused cases and 10 stress checks passed; 1,000 layout updates, 500 alternating sizes, 500 selection transitions, zero allocation, zero drift. |
| C174 shared policy/transaction | 22 focused cases passed, including minimum/maximum, exact bounds, one-pixel rejection, layout/render/state rejection mapping, rollback sizes, no-op, and independent state. |
| C174 stress | 1,000 transitions, 500 alternating cycles, 500 independent-window transitions, 500 rollback checks; zero allocation and zero drift. |
| Settings UI regressions | C167 20 cases / 60 stress iterations and C168 31 cases passed. |

The evidence manifest and logs are under
`out/dotnet/c174-shared-managed-resize/host/`. The manifest records the suite
inventory, source HEAD, log hashes, and zero-allocation results.

No timing benchmark was captured in the accepted C172/C173 baseline, so C174
makes no claim of lower execution time. Candidate validation adds bounded
integer comparisons and a titlebar subtraction; the measured hot-path
allocation remains zero.

## 5. Guest qualification

The C174 source passed three focused QEMU boots and three proof-disabled
ordinary production boots. Focused manifests report C173 Outcome A and verify
resize callbacks, rejected geometry rollback, focus changes, independent
window identities, Explorer path/selection/scroll state, and Notes activation.
The ordinary lane exercised the real compositor drag path from the clean
production candidate and verified the same behavior from screenshots, QMP
input logs, and application state checks. The ordinary serial logs contain no
C173 proof-only geometry diagnostics.

The proof-disabled concurrent-app run opened Settings and Explorer together.
Settings retained its 560×340 geometry while Explorer resized to 960×526 and
600×360; Settings then resized to 512×222 and back while Explorer remained at
600×360. The two windows kept distinct surface identities. Explorer retained
`/system/apps/C164` and the `hello.txt` selection, focus returned to each app,
Notes activation passed, and the pending Settings association slots stayed
unchanged. Focused and ordinary runs also rejected one-pixel-under-minimum
Explorer sizes and retained the prior layout and state.

Durable evidence is under
`out/dotnet/c174-shared-managed-resize/`:

| Evidence | Result | SHA-256 |
| --- | --- | --- |
| Host regression manifest | PASS, 24 suites / 736 cases | `AAC0EA287DFAF006F3E0907BD94F39C98F72DB5CF32595410857119B96C2B7A6` |
| C172 focused proof manifest | PASS, 3 boots | `0239C969EF945EF983586494D37F92FF96DCFF4D8756019F63A28088FE0D9B81` |
| C173 focused proof manifest | PASS / Outcome A, 3 boots | `D2F78AD2B6B2DDCCC1A9483D8935D6EA1797551C8DCCDB2455F895A7781DC546` |
| C172 production acceptance manifest | PASS / Outcome A, 3 boots | `51A8A2E21F37EBBA53A8910B1CD0F0C3F9F6C5DCB230229388CF6852E2952F86` |
| C173 production acceptance manifest | PASS / Outcome A, 3 boots | `41729B4CCE3E8EAD2CC8E0A2E58D5A6008CA670920931209F64A199605C55696` |
| C166A2 ordinary production manifest | PASS / Outcome A, 3 boots | `49F35F2483E163B983746917674244E55E4A991C8600F135A23DDD24517C0B31` |

The proof-disabled candidate hashes were composite
`4673B7E1C20D8B495747435BEFA146A549657D61217F6B0EEC5CAB22AA7F1956`, kernel
`9F3091916D7CB6C109BB5D71B65E1D7F1047AB576C8C994105179ECCB6E6A8F1`, and
ramdisk `B93E73868F9FB6936B5205275F5E7E793D8D09627D1125F89C7C8DA5D00F76BA`.
The three proof-disabled serial/QMP log pairs are recorded in
`production-acceptance/c166a2-manifest.json`; their serial SHA-256 values are
`576905398D6DC05D6E6E699C47180B92FCE93D5DEF3C7CDFF4D242847D66753B`,
`4BDFE9CC90BBCB0360612B11F560932C0FB324597D4EDF45C2CEB5692FDF7CAB`, and
`B8CD6AC6E1FE8F7CD0E0CA91B7072FED3763A186AEB6B322FC3CC7E4A91F9666`.
The QMP log SHA-256 values are
`75C1F1A6FFA862A0A6FC4718B61A955B3B9A0DEED6E55AEE0FAEBAA3D4D94F10`,
`E64A105FD1576C7CF9CEB5FA8901171ACCAD266973CAA19302B7F1194570DD35`, and
`38093791BDFA66DB53E26B4AF96DABEFDA2D84CF7858EDD6E8A948194099295C`.
The concurrent Explorer expanded screenshot
`ordinary-boot3-c173-explorer-concurrent-expanded.ppm` has SHA-256
`B9D0CD462E28C46A6B4114EE1EE9BA71ADBB8E10F0089FF1C75FE47DD1109F02`.

After all boots passed, the acceptance script installed the candidate into the
canonical ESP. I restored the pre-existing `ESP/ramdisk.img` from its preflight
backup and verified both files hash to
`AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` in the
final worktree. Candidate media and each QEMU run used isolated `out` artifacts.
External QEMU processes owned by other projects were left untouched; the C174
runner reaped its own QEMU process and reports zero unrelated processes
terminated.

## 6. Compatibility and remaining limits

The host ABI remains v4 / 128 bytes, with callbacks at offsets 104, 112, and
120. No native compositor callback or kernel interface changed. The existing
managed action selectors, NativeAOT interop, association persistence, and
`GXSETT.BIN` v2 / 26-byte format are unchanged. The existing application
instances and compositor windows remain the resize owners. Settings and
Explorer keep their own limits, control placement, focus, scrolling, and
selection policy.

Production compatibility passed focused and proof-disabled guest qualification
with clean candidate media. The C173 production manifest confirms the same
source head, three focused boots, three ordinary boots, no proof-only C173
diagnostics in ordinary serial output, and intact media baseline. No C175 work
is included.
