# REXX RX32 — Independent Built-in Selection

## Preflight and scope

- Required branch: `REXX_SUPPORT`; observed branch: `REXX_SUPPORT`.
- Accepted and observed starting HEAD: `475b19a68b38d747d09dadcfc192c6126947856f`.
- Tracked worktree and index were clean. There was no root `.phase`, no RX32 document/test/evidence path, and no prior RX32 implementation at this HEAD.
- RX31 inventory has 75 entries: 59 Implemented, 11 Not implemented, 3 Partial, 1 Not assessed, and 1 Not implemented by design.
- The eleven Not implemented entries are the candidates below. `REGINA_EXTENSION_SET` remains the one Not assessed entry; “No functions proposed” remains Not implemented by design.
- There are 31 protected root `.obj`/`.pdb` files. Their canonical path/size/SHA-256 snapshot is recorded in `evidence/rx32/preflight.json` and its manifest.
- There were 100,478 pre-existing untracked paths. The final precommit path set was independently enumerated after RX32 files were staged: 107,304 unique paths, 6,826 additional paths, and zero missing baseline paths. Gzipped start/end/addition manifests preserve canonical UTF-8, LF-delimited, ordinal-sorted path lists; evidence/rx32/untracked-reconciliation.json records payload and compressed hashes. Existing historical evidence is preserved.
- The QAC CLI was located at D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe and its file identity was verified (GuideXOS.QAC.Cli 1.0.0.0; SHA-256 F7089D508C1D8307E11DE1B944B6E1087C0384901333E8228EC117291CA8DF6). An unrelated managed acceptance reservation was observed during preflight, so RX32 did not touch it. Once it cleared, QAC status/list and the QEMU census were empty before launch. RX32 then acquired exclusive acceptance reservation 5e472717be7047f5b48d0c80065c8508 for QEMU PID 22212; final status/list and QEMU census returned to zero. RX31 native acceptance remains open.

## Candidate matrix

| Function | RX31 classification | Classic/IBM portability | Existing related implementation and required runtime support | Dependencies, resource/security implications | RX32 disposition |
|---|---|---|---|---|---|
| CHARIN | Not implemented | Classic stream I/O built-in | No stream BIF/provider; needs named/default input stream, byte/position handling | Host stream ownership, blocking and file access policy; avoid | Defer |
| CHAROUT | Not implemented | Classic stream I/O built-in | No stream BIF/provider; needs output stream writes and position rules | Host writes and path/stream security; avoid | Defer |
| CHARS | Not implemented | Classic stream I/O built-in | No stream BIF/provider; needs stream status/remaining-byte reporting | Host stream metadata and stream identity; avoid | Defer |
| LINEIN | Not implemented | Classic stream I/O built-in | RX31 has a bounded PULL provider, but it is not a named-file/stream provider; line position/count semantics are separate | Host stream lifecycle, blocking, encoding, and errors; avoid aliasing PULL | Defer |
| LINEOUT | Not implemented | Classic stream I/O built-in | No named-stream writer; SAY/output writer does not implement stream selection or line-number semantics | Host writes and file/path security; avoid | Defer |
| LINES | Not implemented | Classic stream I/O built-in | No stream status provider | Host stream metadata and implementation-dependent options; avoid | Defer |
| STREAM | Not implemented | Classic stream I/O built-in | No stream command/status dispatcher | Would introduce open/close/query host I/O and path policy | Defer |
| VALUE | Not implemented | Core active-variable-pool behavior is classic/SAA. CMS global-pool `selector` forms are an IBM z/VM extension. | VM already has per-instance local/global/stem environments, `environment_load_variable`, `environment_set`, and compound-tail resolution. Needs a small VM-facing dynamic variable get/set API and dispatcher entry. | No OS service. Name length is bounded at 65,535 bytes; stored/result strings remain within the VM’s 16 MiB limit. State stays in the active VM environment. | **Implement now** |
| EXTERNALS | Not implemented | IBM TSO/E non-SAA built-in | RX30 supplies one VM-owned external data stack, but EXTERNALS reports host/execution context beyond this stack | TSO/E execution-context semantics; not a portable extension of QUEUED | Defer |
| LINESIZE | Not implemented | IBM TSO/E non-SAA built-in | No terminal width/stream record-size interface | Terminal/host environment dependency; no freestanding source | Defer |
| USERID | Not implemented | IBM TSO/E non-SAA built-in | No VM identity lookup | Kernel identity/account service and security boundary | Defer |

## Mandatory selection checkpoint

**Selected function:** `VALUE`, confirmed as Not implemented in `REXX_RX31_BUILTIN_INVENTORY.csv` and absent from the built-in dispatcher.

**Reason:** It is the only remaining classic candidate that can operate entirely on interpreter-owned state. Its current-scope variable lookup and assignment can reuse the VM’s existing scope, stem, exposure, compound-tail, allocation, and value-copy machinery. It is deterministic and requires no new operating-system service.

**Reference:** IBM documents `VALUE(name,newvalue)` for the current REXX variable environment, including dynamic compound-name substitution, old-value return on assignment, and the rule that an uninitialized variable returns its default name value without raising NOVALUE: [IBM z/OS 3.2 REXX VALUE](https://www.ibm.com/docs/en/zos/3.2.0?topic=functions-value). IBM z/VM documents CMS global-pool selector behavior (`GLOBAL`, `LASTING`, `SESSION`, or a group name), which depends on CMS services and is not part of this kernel phase: [IBM z/VM 7.2 REXX VALUE](https://www.ibm.com/docs/en/zvm/7.2.0?topic=functions-value).

**Existing reuse:** `rexx_vm.c` already resolves current-environment locals, exposed names, stems, compound tails, and default uninitialized values in `environment_load_variable`; `environment_set` applies writes to the active variable generation. `rexx_builtin.c` already has a VM-aware context-function dispatcher and a symbol-name validator used by `SYMBOL`.

**Scope exclusions:** CMS/global/external variable pools and third-argument selector services; all stream functions; TSO/E-only `EXTERNALS`, `LINESIZE`, and `USERID`; changes to DATE, TIME, TRACE, queue services, host input, or the RX31 recorded Outcome B.

**Expected inventory change:** `VALUE` changes from Not implemented to Implemented for the portable current-REXX-variable-pool signature `VALUE(name[,newvalue])`. The IBM z/VM CMS global-pool selector extension remains explicitly unsupported and documented. Expected totals after qualification: 60 Implemented, 10 Not implemented, 3 Partial, 1 Not assessed, and 1 Not implemented by design.

## Scope gate result

The starting HEAD matches the requested RX31 baseline, and no RX32-equivalent work exists. RX32 may proceed with the bounded `VALUE` implementation. No branch, history, or worktree operation was performed.
