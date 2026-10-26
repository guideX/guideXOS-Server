# NativeAOT C188R11R2R1 Guest Retry

**Run date:** 2026-10-25 (America/Los_Angeles)

**Classification:** **Outcome A — `MANAGED_DISPATCH_QUALIFIED`**

**Repository:** `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`

**Branch:** `v1.1_DOTNET_SUPPORT`

**HEAD at guest-run start and shutdown:** `8b905c67b96850467984ea2b81d5206e97d91e16`

## Result

One QAC-owned guest received exactly one ordinary `a` key-down/key-up transaction. It reached the native managed-dispatch boundary and managed registry dispatch. The input snapshot captured an empty descriptor table and no resident applications: `rg=1`, `re=1`, `n=0`, `m=0`. `TryFind(4)` rejected the event with `-4`. **Managed Notes' callback was not reached**, so this run does not show Notes handling the key or inserting text.

The run reproduces the input-side empty-registry state reported by C188R10R1 and localizes it after keyboard routing and at managed input dispatch. The reason the managed state differs from the successful Notes launch return remains undetermined.

## Repository and preservation

- Actual starting HEAD was `8b905c67b96850467984ea2b81d5206e97d91e16`; the branch was `v1.1_DOTNET_SUPPORT`, one commit ahead of its origin tracking branch. HEAD was still that commit when the guest shut down. This report and the focused runner corrections were made as closeout changes after the run.
- The pre-existing `ESP/ramdisk.img` still hashes to `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`.
- The pre-existing generated and untracked paths under `samples/host/ManagedHostServicesC185`, `ManagedStarterC182`, `ManagedUiRegression`, `samples/managed/C185CounterApp`, `HostLogProof/c182-host-smoke`, and `ManagedUiContracts` were retained.
- No reset, clean, stash, branch switch, history rewrite, or foreign process termination was performed. The new guest evidence is isolated at `out/dotnet/c188r11-managed-state-provenance/runs/diagnostic-003/guest-001/`.
- The existing unique-evidence-path guard was satisfied: `diagnostic-003/guest-001` and this report did not exist before the run. The QAC guest runner also required the expected diagnostic kernel hash and an empty QAC census.

## QAC admission and shutdown

The preflight showed the QAC coordinator online, zero QEMU processes (zero foreign), zero reservations, and zero managed sessions or queued work. QAC granted exclusive acceptance reservation `18dd4e68f7df435fa21fc4f71ca92f4e` in 1.1 seconds. Its list reported the reservation owner as verified and the guest as running. The only QEMU child was PID `27752`, launched under QAC runner PID `32620`; no foreign QEMU process was involved.

After the one QMP `quit`, QAC recorded guest exit code 0 and released the matching reservation. Postflight showed zero QEMU processes, zero reservations, and zero managed sessions. No degraded census warning was recorded. The full preflight, during-run, and postflight status/list captures and QAC logs are in the guest evidence directory.

## Reused diagnostic artifacts

No builds or hosted suites were rerun. The retained qualification manifests and copied guest media were verified:

| Artifact | Result / SHA-256 |
|---|---|
| Diagnostic NativeAOT qualification manifest | PASS; manifest `6A8DEB50D0761E5549FDEEF48F8875D1C62EBA269B2359E769EFD9704279CD69` |
| Diagnostic NativeAOT ELF | `6B41883A2BDF6F8DCA3448ABB5459C6456D7842B2BBC7973828B2FE7DE78750C` |
| Diagnostic kernel qualification manifest | PASS; manifest `2859FBB6932D1238621A3A3163B89DA8D4957C80A9ED2EB537151F0707DC93A0` |
| Diagnostic kernel ELF | `0032BD297D86314D86B182C8CFDC7A15E3C12FE07BDEE5F192A2BFFD78403B93` |
| Candidate ramdisk staging manifest | PASS; manifest `75DD947E393CA6FA6FA9103F313B73FE28AF1A9BD372E7507FAE5C51B0495581` |
| Candidate staged ramdisk | `6F673BA41E7245CB1BA7B7857327A4EE74212F09559A920D2B267710F3E00A9D` |
| Candidate input/copy manifest | PASS; `r2-candidate-input-manifest.json` hash `95AADA28969E41C310605B7300AE24DC1BB0652456FD16707DA84860D0780E83` |

The run's `acceptance-manifest.json` independently records matching guest media hashes: kernel `0032BD...`, ramdisk `6F673B...`, and UEFI loader `D72A35...`. The staged app hash matches the qualified NativeAOT ELF. The source ramdisk remained unchanged.

## Guest readiness and selected input

The guest reached the input main loop at `1280x800`. Managed Notes launched through the normal desktop path with result `PASS`. Serial recorded focus on window `000003E8`, title `Managed Notes`, owner `Managed NativeAOT`. The pre-input screenshot shows the Notes desktop and window; its existing focus diagnostics were captured. No focus-changing click was sent.

Exactly one QMP qcode `a` transaction was recorded: one key-down and one matching key-up. The expected Set-2 make scancode was `0x1C`, which the kernel decoded as printable ASCII `a` (`0x61`). The QMP transcript records two `input-send-event` requests, one down and one up, followed by one owned shutdown command. No second key or guest boot was issued.

The bounded serial trace confirms these stages in order:

1. PS/2 IRQ accepted scancode `0x1C`, queued event ID `1`, value `0x61`.
2. Kernel dequeued the printable key with no pending-slot overwrite.
3. Compositor routed event `1` to the focused Managed Notes window.
4. Native managed entry ran for selector `4`, key-char value `0x61`, call/correlation `2`.
5. Managed dispatch entry and its registry snapshot ran.
6. Descriptor lookup rejected selector `4` with result `-4`; the native managed return was `0xFFFFFFFC`.

## Registry and callback state

| Boundary | Registry identity / epoch | Descriptor count | Resident mask | Notes generation | Other state |
|---|---:|---:|---:|---:|---|
| Successful launch return, managed sequence 13 | `1 / 1` | 5 | 1 | 1 | pool identity 1, pool-storage identity 1, pool epoch 1, generation high-water 1 |
| Input dispatch entry, managed sequence 16 | `1 / 1` | 0 | 0 | 0 | pool identity 1, pool-storage identity 0, pool epoch 0, generation high-water 0 |
| Input lookup, managed sequence 19 | `1 / 1` | 0 | 0 | 0 | selector 4 returned `-4` |

At input entry, replacement, registry-clear, pool replacement, pool-clear, reset, and teardown counters were all zero. The kernel recorded one image mapping, one managed registry/pool initialization event, and the same NativeAOT module, entry offset, segment count, and span at launch and input. The input call recorded `C188R11-HOST ready-reentry` with host epoch 1, ready-call count 1, registry epoch 1, and pool epoch 1. No second `C188R11-INIT` marker appeared.

The `notes-callback-entry` and callback-return markers are absent. Registry lookup rejection is the last confirmed managed boundary. The pre-input and post-input screenshots have identical SHA-256 values; this is visual evidence only and is not treated as callback or text-insertion evidence.

## Runner failure boundary and focused corrections

The QAC guest runner itself completed with `acceptance-manifest.json` result `PASS` and verified release. The outer R2 controller exited with code 1 because its already-loaded focus regex used `owner=\S+`; that rejected the valid multiword owner `Managed NativeAOT` and held the controller before input. The serial focus record was rechecked immediately before using the existing `c188-qmp-control.py` helper against the same active QAC-owned guest for the single authorized transaction. The wrapper later reported that the inner runner exited before its R2 qualification predicate, because the controlled QMP quit had already shut down the guest. It therefore did not write its usual `r2-keyboard-dispatch.json`; the QAC acceptance manifest, QMP log, serial trace, screenshots, and this observed-input evidence record preserve the run.

The wrapper now verifies the retained candidate staging manifest and expected kernel hash, then copies the isolated ESP into the unique run directory and verifies the copied media hashes. Its source focus matchers and the predicate helper now accept multiword owner names. The R2 lookup parser now selects the `st=lookup` record after `input-dispatch-entry`, so an earlier successful launch lookup cannot mask the input-side rejection. The corrected parser was applied to this serial log and reported `managedStateQualified=true`, registry count `0`, resident mask `0`, lookup result `-4`, and no Notes callback. No hosted suite or rebuild was run.

## Remaining defect and next action

**Demonstrated defect:** between successful launch return (sequence 13) and the first managed input snapshot (sequence 16), the registry reports the same identity and initialization epoch but its descriptor count and resident mask change from `5/1` to `0/0`. Input dispatch reaches `TryFind(4)` and receives `-4`. Notes' callback does not run.

The smallest justified next action is a focused audit of the managed registry and lifetime backing state across those two NativeAOT calls. Reconcile the observed `rg=1/re=1` with `n=0/m=0`, `ps=0/pe=0`, one initialization marker, and the host's same-epoch `ready-reentry`. Identify the specific state transition or storage boundary before making a runtime repair, then add a targeted regression for that transition. The input-routing path already reaches managed dispatch; no Notes handling or text insertion is claimed.

## Evidence files

- `out/dotnet/c188r11-managed-state-provenance/runs/diagnostic-003/guest-001/r2-observed-input-evidence.json` — outcome and parsed input/registry evidence.
- `out/dotnet/c188r11-managed-state-provenance/runs/diagnostic-003/guest-001/serial.log` — complete serial trace; SHA-256 `51C400F9C243167038C86550B0BB0CBFBCE1193E5B355A051D5D29FDFC77ECCC`.
- `out/dotnet/c188r11-managed-state-provenance/runs/diagnostic-003/guest-001/qmp-control.log` — QMP screenshots, exactly one down/up pair, and quit; SHA-256 `4BDFEB9CC972F736577A76DB4D75C7015A42863F6C41F82184A1DC1F7416E519`.
- `out/dotnet/c188r11-managed-state-provenance/runs/diagnostic-003/guest-001/acceptance-manifest.json` — guest build identity, readiness, and verified QAC release.
- `out/dotnet/c188r11-managed-state-provenance/runs/diagnostic-003/guest-001/before-input.png` and `after-input.png` — both hash to `C00127688E2C2193CB40D54D3A0DAFA887AC11FE1CCA0D97DE5D877A9FA18A86`.
