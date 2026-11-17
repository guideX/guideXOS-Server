# NativeAOT R32 Thread Construction and Stack-Bounds Qualification

**Outcome B — `THREAD_ATTACHMENT_NATIVE_VERIFIED`.** The corrected candidate completed stock NativeAOT thread attachment, passed the pre-allocation GC-state guard, executed and returned from stock array allocations, and completed generated `InitializeModules`. The selector-5 `ManagedSettingsCenter` factory was not entered.

Exactly one QAC-owned guest boot was consumed. QAC release and guest cleanup were verified. There was no retry and no push.

## Repository and provenance

- Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`, branch `v1.1_DOTNET_SUPPORT`.
- Expected and actual starting HEAD: `249f5e9e04439b21c861a8d4b551758677bd852b`.
- Candidate source HEAD: `79f0036c6cdd1e48ee9e73cbfa03c072bc120375`.
- Implementation HEAD immediately before this report: `d598f5c1dc77057c03e8cc433bcf56bfe92dd415`.
- Final HEAD: the report commit containing this file; its exact OID is recorded in the task closeout.
- Runtime source: pinned NativeAOT/runtime `9.0.0`, commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
- No push was made.

The candidate retains the R30 stock reverse-P/Invoke trace profile, Workstation GC, the accepted R20 unwind repair, and the R29 pre-allocation guard. It links the real `GuideXosApplicationRegistry` selector-5 factory to `ManagedSettingsCenter` (`ManagedSettingsCenter___ctor_0`). The booted candidate was built with runner source hash `BE0F8E1AD753ACA1BDEE90CBF798A5B346EE29D13AED4C6933F8B47E5D30C903`. The later parser correction changed only `run-c188r30-thread-attachment.ps1` and its regression test; it was committed after the boot and did not change the candidate ELF, kernel, or runtime-pack binaries. The correction and its tests are reported separately below.

## R31 boundary and cause

R31 candidate `candidate-20261115T211220185Z-33d0d240002c4be298cc6df59c3af9f8` is preserved under `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r30-thread-attachment`. Its serial log SHA-256 is `4F32D4FBA10021DE5D534A1D0DF02A1930B6385590060F6D13CD1EEC02A5A00A`.

The exact FLS completion event was `pal_fls_set_value raw0=0 raw1=0x384F290`, followed by `pal_attach_complete`. The event is emitted by guideXOS's custom `PalAttachThread` after the FLS set succeeds. In the stock call path, `RhpReversePInvoke` enters `RhpReversePInvokeAttachOrTrapThread2`, then `ThreadStore::AttachCurrentThread`; that function calls `PalAttachThread`, then `Thread::Construct`. R31's later successful `pal_thread_id` event establishes that PAL attachment returned and construction progressed into a constructor PAL dependency. It does not by itself establish constructor completion.

R31 had no stack-bound, attached-state, ThreadStore publication, transition-attach completion, or allocation event. Therefore the guest did not demonstrate ThreadStore lock acquisition, `TSF_Attached`, list publication, or successful construction. Pinned source places these later operations after the PAL attach and in the constructor/publication sequence, but the absent events alone do not locate a failure.

The candidate map resolved `PalGetMaximumStackBounds` to stock `Runtime.WorkstationGC:PalRedhawkCommon.cpp.obj`. That implementation calls `VirtualQuery`, checks that the returned byte count equals `sizeof(MEMORY_BASIC_INFORMATION)`, and uses the TEB's `StackBase` and `AllocationBase`. The selected guideXOS startup import `guidexos_nativeaot_startup_VirtualQuery` returns zero unconditionally. Pinned `Thread::Construct()` calls `PalGetMaximumStackBounds(&m_pStackLow, &m_pStackHigh)` and invokes `RhFailFast()` when it returns false. Together, the linked map, these source implementations, and the R31 trace after thread-ID retrieval establish a deterministic broken stack-bounds dependency in the selected profile. R31 did not emit a stack-query result, so the source and link analysis—not the invalid failure record—establish this cause.

The pinned constructor also obtains current process/thread handles and attempts `PalDuplicateHandle` before the stack query. Those handle operations do not have individual R32 trace events, so their exact values and duplication result are not claimed as observed. The stock `PalDuplicateHandle` attempt is not treated as a cause: the pinned constructor does not fail based on its result. Successful later ThreadStore publication establishes that `Thread::Construct()` returned.

R31's generic fail-fast was `reason=43525402`. The startup `RaiseFailFastException` import forwarded straight to the kernel fail-fast hook and skipped the R25 first-failure writer. The schema-3, 192-byte record at `0x1001E34D0` contained only zeros and failed validity checks; no earlier valid record exists. A narrow correction now calls `guideXosNativeAotCapturePalFailFast(0x43525402)` before forwarding the same fatal reason. It preserves R25's first-writer rule and adds no allocation or blocking lock to the fatal path. R32 did not fail fast, so this corrected capture route was not exercised by native failure.

## Repair and stack ownership

The R30 profile now removes stock `PalRedhawkCommon.cpp.obj` and selects exactly one `guidexos_nativeaot_pal_common_replacement.cpp` object. The candidate manifest records zero remaining stock PAL common members, one replacement member, and the replacement object hash. Candidate-linked map verification confirms `PalGetMaximumStackBounds` resolves to `Runtime.WorkstationGC:guidexos_nativeaot_pal_common_replacement.threadstore.obj`, while stock `Thread::Construct`, `ThreadStore::AttachCurrentThread`, and the stock reverse-entry/return helpers remain linked.

The replacement keeps the NativeAOT PAL surface: `extern "C" bool __stdcall PalGetMaximumStackBounds(void** low, void** high)`. Under the x64 Windows ABI it receives two output-pointer arguments and returns `true` only for valid bounds; `false` means unavailable or invalid. It calls the fixed-width guideXOS PAL contract with three `uintptr_t*` outputs (low, high, current); the contract returns `0` on success. The adapter copies the real low/high values, validates `low < high` and `low <= current < high`, and fails closed on a missing hook or invalid values.

The kernel hook uses the current thread control block's owned `stack_base` and `stack_limit` and obtains the actual stack pointer from `arch::context::get_sp()`. For the bootstrap thread those bounds point to `boot_stack_bottom` and `boot_stack_top`, the linker-owned 16 KiB reservation in `kernel/arch/amd64/boot.asm`. No guessed window or fabricated bounds are used. The shared stack-bounds API additionally rejects null output, unavailable thread context, inverted/empty intervals, and a current pointer outside the interval.

Native R32 recorded:

| Value | Observed |
|---|---:|
| Low, inclusive | `0x517C000` |
| High, exclusive | `0x5180000` |
| Current stack pointer | `0x517F038` |
| Reserved interval size | `0x4000` (16 KiB) |

The pointer is inside the actual current-thread interval. The PAL returned success, allowing stock construction to proceed.

## Candidate and integrity evidence

Fresh candidate root:

`E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r30-thread-attachment\candidate-20261117T010609533Z-8efc5f55211c4e5d8c5500d79b0b769c`

| Artifact | SHA-256 / result |
|---|---|
| Candidate manifest | `787A46FFFF09C23355E585F6A7C02BC1ABA19429DED156BC64C19F28A6F510E2` |
| Candidate integrity inventory | `72B913375CA2438C4FB3705D3B1F6AA514DBA0DDB1A46F5F964DEA44BBF89A94` |
| Inventory | 349 files, verified |
| R30 candidate manifest | `E53890CD139BFD52AD915E241B339CD4F08A79DE26F308148EE00C2F19114439` |
| R25 candidate manifest | `9D7246718CAE6771682C76E4C07EA8C393E2507221B591D95640CD079235C46D` |
| Runtime-pack manifest | `D4AB05BAB6D2B8ED8859BF11B41A8B173B49C910D9C4D3FAA14AE44191C36FAD` |
| Managed ELF | `14C61298B00665B9981C7FE177670C529C69B4F8AF2D9EA96BF4C53F888C40C8` |
| Managed map | `086F3B7B6E81E0743BF733CA4034F6D5789A307D1BAB2CC6FE51706C3BF8C3A4` |
| Kernel | `9931827D7F6186FF98E1DA580A0B3ED2EAAF725359A7EC096F0ED16ADF495D6B` |
| Linked R30 qualification | `B000281B8914D91953D31688BC326DE83B2555F727B398E275A380B7159273F4` |
| R25 analyzer | `1E7E836B60330BACD4FF9ECE796C9AD6D7C62051E24DAC565390E112B66A2013` |
| PAL replacement object | `430AB33B8FBD2B510CB878CD1D16381C51082910EEF8020A8B315523E611B6C2` |
| Stock PAL common members left / replacement members | `0 / 1` |
| R20 accepted unwind source | `9F94B5388535F2224D94318AC30D69260944ADB942C5F77FF5C90F0CDA9A3D3E` |

The real selector-5 `ManagedSettingsCenter` factory is present in the candidate. Its linkage is not evidence that it ran.

## One-shot QAC run and milestones

The fresh no-boot rehearsal and candidate-linked validation completed before admission. R30 and R25 read-only preflights both reported `CLEAR`; QAC was responsive and exclusive, with no host QEMU process. Preflight did not reserve QAC or create a boot guard. The one-shot guard bound the candidate manifest, inventory, kernel, and managed ELF hashes. It records `maxGuestBoots=1`, attempt 1, `consumed=true`, `automaticRetry=false`, and `retryAllowed=false`; the guard SHA-256 is `3AB9D8FB2B40C0F34DE0115E769526F9524EF07657AC7CDBAB259434DA6D7C49`.

The sole boot's serial log is `...\qac-guest-r25-0237e351e4dc423897f5592cc6b9bfb2\serial.log`, SHA-256 `D35F173911D01B6738EDF6027CD75F5938619E616B9A59CB42C09F323592FFBA`. The R25 run record reports candidate integrity verified, outcome `INITIALIZEMODULES_RETURN_VERIFIED`, and `qacReleaseVerified=true`. The child runner returned nonzero only after preserving native evidence because the later bounded Settings handoff did not observe readiness.

| R32 milestone | Result | Native evidence |
|---|---|---|
| M1 `InitializeModules` entered | Pass | `module_initialize_enter`; startup `C188B007`. |
| M2 stock reverse-P/Invoke invoked | Pass | `transition_enter`, then stock attach path. |
| M3 PAL FLS attachment completed | Pass | `pal_fls_set_value` and `pal_attach_complete` for Thread `0x384F290`. |
| M4 `Thread::Construct()` entered | Pass | Successful `pal_thread_id` event occurs inside the constructor after PAL attach; the subsequent stack-bounds PAL call also executes. |
| M5 constructor PAL dependencies completed | Pass | OS thread ID `1` returned successfully; stack-bounds call returned valid bounds. Stock constructor returned, as shown by later stock attachment publication. |
| M6 valid current stack bounds | Pass | `0x517C000 <= 0x517F038 < 0x5180000`; PAL result is success. |
| M7 ThreadStore lock/publication | Pass, source-backed | Stock `AttachCurrentThread` acquires the ThreadStore lock after construction, then sets `TSF_Attached` and links the Thread. The observer's separate read lock found Thread `0x384F290` in the list (`threadstore_publication raw1=1`). The original write-lock acquisition is inferred from the linked stock path and subsequent state/list evidence; no separate lock-acquired event was emitted. |
| M8 attached state published | Pass | `transition_attach_state raw0=1 raw1=0`: `TSF_Attached` is set; transition frame is null, consistent with cooperative mode. |
| M9 initialized current Thread available | Pass | `transition_attach_fls` and `transition_attach_complete` identify the same Thread; current ThreadStore accessor contract and list observer both pass. |
| M10 pre-allocation GC-state guard | Pass | `rhp_array_gc_state_valid` for Thread `0x384F290`. |
| M11 stock `RhpNewArray` executes | Pass | Stock allocator enter/return and wrapper return events; eight allocations returned valid object pointers. |
| M12 module initialization advances/returns | Pass | `module_initialize_complete`; R25 outcome `INITIALIZEMODULES_RETURN_VERIFIED`. |
| M13 selector-5 Settings factory entered | Not reached | No selector-5 factory marker. The later bounded Settings handoff ended before readiness, with `clickAttempted=false`. This does not negate completed thread attachment or module initialization. |
| M14 QAC release and guest cleanup | Pass | R25 run record reports release verified. Read-only post-run QAC status/list showed Online with zero QEMU processes, reservations, or sessions; host process census found no QEMU. |

The serial trace also shows normal selector-4 Notes activity after module initialization. That is a different factory and does not establish selector-5 Settings construction or completion. The R25 first-failure record is invalid because this successful native path did not issue a fail-fast; it is not evidence of a new runtime failure.

## Post-boot evidence packaging correction

The R30 wrapper ran its child successfully and preserved the `INITIALIZEMODULES_RETURN_VERIFIED` record, serial, and QAC cleanup evidence. Its final packaging then failed while parsing the returned FLS sentinel: PowerShell interpreted `[uint64]0xFFFFFFFF` as signed `-1`, which cannot be cast to `UInt64`. The post-boot source fix uses decimal `[uint64]4294967295` and adds regression assertions. The corrected parser and candidate-linked suite passed. This was an outer evidence-packaging error after the guest run, not a NativeAOT or guest failure. The consumed guard was not reused and the guest was not rerun.

## Hosted qualification

| Qualification | Result |
|---|---:|
| R30 linked thread-attachment suite, final source | 25 passed |
| R29 pre-allocation GC-state guard | 19 passed |
| R28 GC-state guard | 16 passed |
| R25 first-failure Python suite | 21 passed |
| R25 first-failure PowerShell suite | 34 passed |
| R24 initialization assertions / snapshot | 39 / 14 passed |
| R21 / R22 / R23 regressions | 9 / 29 / 12 passed |
| R26 first-failure / pre-created evidence-root suites | 13 / 18 passed |
| R27 prelaunch rehearsal | 13 passed |
| R12 ThreadStore readiness / telemetry | 7 / 6 passed |
| R12 QAC admission | 35 passed |
| R20 preserved-candidate iterator replay | 31 assertions passed; no guest boot |
| Hosted native stack-bounds smoke, bare-metal stack/process builds, inactive ThreadStore probe | Passed; full kernel build skipped by that smoke command because the production candidate build performed it |
| No-boot R30 runner rehearsal | `QAC_PREFLIGHT_RESULT=CLEAR`, no guard, no QEMU launch |
| PowerShell syntax and corrected sentinel parser | Passed |

R20's accepted GC result remains preserved; R32 did not reopen that investigation. The earlier rehearsals that used incorrect R20 test arguments and the first candidate rejected by an incorrectly addressed manifest property are preserved as failed rehearsals, not counted as passing qualification. The fresh corrected candidate and the final R20 replay passed.

## Protected files and next step

The three pre-existing modified objects remain untouched and were not staged. The first object's correct 64-character hash is independently confirmed from preserved R29 history; the 65-character value transcribed in the R30 report is malformed and is not reused.

| Protected object | SHA-256 |
|---|---|
| `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj` | `3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD` |
| `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj` | `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3` |
| `guidexos_nativeaot_native_alloc_record.obj` | `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183` |

The next step is a fresh hosted candidate and one-shot authorization to continue from completed module initialization toward the actual selector-5 `ManagedSettingsCenter` factory. No R20 GC change is indicated by this evidence.
