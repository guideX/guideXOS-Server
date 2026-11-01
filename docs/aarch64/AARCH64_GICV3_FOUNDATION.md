# AArch64 GICv3 Interrupt Controller Foundation

**Status:** `GICV3_HOSTED_READY_GUEST_PENDING`
**Scope:** EL1, non-secure Group 1, boot CPU only
**Target platform:** generic ARM64 DTB-described systems. Samsung tablet addresses are not assumed.

## Outcome and evidence

The production GICv3 path is implemented and links into the common Phase 4 ARM64 kernel. Hosted register-model tests pass, including parser and register negative controls and a simulated timer event that reaches the real common IRQ registry and scheduler timer path. Phase 2, Phase 3, and Phase 4 freestanding ARM64 builds also pass.

No guest validation was run. This checkout contains no QAC reservation command, provider, or grant file, and no QAC tool is available to this session. The GICv3 guest gate remains pending an actual exclusive QAC grant. Hosted simulation is not guest or ARM exception evidence.

## Existing ARM64 interrupt architecture

The original Phase 2 controller is GICv2. `phase2_gic.cpp` programs GICD/GICC MMIO and exposes the existing acknowledge, IRQ ID, enable/disable, and completion hooks. Phase 2 handles its timer PPI directly. Phase 3 calls those architecture hooks and returns either the interrupted frame or a scheduler-provided interrupt context through the existing exception return bridge.

Phase 4 already owns the common IRQ registry. Its timer handler rearms the architectural physical timer and calls `kernel::scheduler::timer_interrupt(frame)`. Device IRQs are dispatched through that same registry, including the Phase 7 input path. The GICv3 work keeps this dispatcher and return contract; it adds a version selector and controller-specific behavior behind the existing ARM64 hooks.

The platform descriptor now carries the GIC version, translated Distributor range, up to eight Redistributor regions, optional stride, interrupt-cell count, and up to eight DTB CPU affinity values. The identity MMU now maps the selected GICv2 CPU interface or every GICv3 Redistributor region. The generic identity map honors the existing 40-bit PA setting and allocates page tables on demand; its fixed 520-table pool still fails setup if a platform requires more tables.

## DTB discovery and validation

Controller selection is based on DTB compatibility. The parser retains the existing GICv2 compatibles and recognizes `arm,gic-v3` and `arm,gic-v3.1`. Unknown or absent interrupt controllers produce an explicit unsupported-controller diagnostic; they do not fall back to GICv2.

The GICv3 parser reads the Distributor tuple followed by every supplied Redistributor tuple. It also reads `#interrupt-cells`, `#redistributor-regions`, `redistributor-stride`, CPU `reg` affinity values, `status`, and parent-bus `ranges`. It rejects disabled mappings, malformed or truncated DTBs, untranslated addresses, overflow, misalignment, undersized register ranges, overlapping controller/UART/RAM/virtio ranges, invalid region counts, non-three-cell GICv3 interrupt bindings, and strides below two 64-KiB frames or not aligned to 64 KiB.

The freestanding parser keeps a 32-node depth bound. It accepts at most eight Redistributor regions and eight CPU affinity entries. If a CPU list is present, boot CPU affinity must match an entry. The detected physical timer continues to come from the `arm,armv8-timer` DTB interrupt tuples; the observed Tab A8 value, INTID 30, is not hardcoded.

DTB parsing remains separate from register access. `gxos_aarch64_phase2_parse_dtb_ex` returns a status code while the original boolean entry point remains available to existing boot paths.

## Distributor implementation

The driver checks GICD PIDR2 for architecture revision 3 and reads GICD_TYPER for the implemented INTID range, capped at 1020. It does not reset all Distributor enables, groups, or pending state. Initialization changes only the non-secure Group 1 enable and affinity-routing controls, then configures the timer PPI on its local Redistributor.

For SPIs, the driver supports enable/disable, priority, level/edge trigger configuration, pending-state query/clear, non-secure Group 1 selection, and an IROUTER affinity route to the boot CPU. Distributor write-pending polling is bounded. Device SPI state is not mass-cleared during initialization.

## Redistributor implementation

Redistributor discovery walks each DTB region within its declared size and a 1024-frame traversal limit. It compares `GICR_TYPER.Affinity_Value` with normalized `MPIDR_EL1` affinity, checks Redistributor architecture revision, then clears `ProcessorSleep` and polls `ChildrenAsleep` with a finite limit. A provided stride is honored. Without a stride, traversal advances by 128 KiB, or 256 KiB when the frame advertises VLPIs.

The boot CPU's SGI/PPI frame handles PPI group, priority, trigger, enable/disable, and pending registers. The generic timer PPI is assigned Group 1, level-sensitive configuration, priority `0x80`, and enabled locally. No secondary CPU Redistributor is initialized, and SMP is not implemented.

## CPU system-register interface and EOI

The production backend uses EL1 system-register accessors for:

| Register | Use |
| --- | --- |
| `ICC_SRE_EL1` | Enable the EL1 system-register interface and read it back |
| `ICC_PMR_EL1` | Accept all configured priorities |
| `ICC_BPR1_EL1` | Configure Group 1 binary point |
| `ICC_IGRPEN1_EL1` | Mask during setup, then enable non-secure Group 1 |
| `ICC_IAR1_EL1` | Acknowledge Group 1 interrupts |
| `ICC_EOIR1_EL1` | Drop priority and deactivate in combined mode |
| `ICC_CTLR_EL1` | Select and verify combined EOI/deactivation mode |
| `ICC_DIR_EL1` | Present only for split mode; split mode is rejected at init |

Before enabling the interface, the driver checks `ID_AA64PFR0_EL1.GIC`. If EL1 SRE is not writable/readable as enabled, if the Distributor or Redistributor architecture revision is not 3, or if combined EOI mode cannot be selected, initialization returns an unsupported-capability diagnostic.

The common exception path obtains the controller acknowledgement, rejects special/out-of-range IDs without EOI, dispatches valid INTIDs through the existing Phase 4 registry, and completes valid unhandled interrupts to avoid a storm. In combined mode, completion writes the original acknowledgement to EOIR1 and does not write DIR. The interrupted-frame versus scheduler-frame return behavior is unchanged.

## Timer and scheduler integration

The production route remains:

```text
ARM physical generic timer
  -> DTB-selected PPI
  -> GICv2 or GICv3 acknowledge
  -> existing exception dispatch
  -> common IRQ registry
  -> Phase 4 timer accounting and rearm
  -> common scheduler timer_interrupt(frame)
  -> existing exception return
```

The hosted integration test supplies a deterministic GICv3 register model and INTID 30 acknowledgement. It passes that event through `kernel::irq::dispatch`, the timer accounting handler, and the real `kernel::scheduler::timer_interrupt` implementation using a hosted architecture-context stub. It verifies the scheduler chooses its completion interrupt context after the configured preemption target. This proves the hosted callback flow only; it does not claim a real ARM exception, timer, or guest preemption.

## Register-access abstraction and hosted tests

`gicv3_controller.cpp` receives bounded MMIO, system-register, and barrier callbacks through `RegisterIo`. The ARM64 production backend uses volatile 8/32/64-bit accesses and `dsb sy`/`isb`; hosted tests provide in-memory Distributor/Redistributor/system-register models. Production code has no hosted threading, exception, or allocation dependency.

The current hosted suite reports **62 assertions passed**. It covers GICv2 detection, GICv3 and unknown-controller detection, malformed/truncated DTBs, translated MMIO, multiple Redistributor regions, overlap/size/stride/overflow rejection, affinity matching, bounded Redistributor wake and RWP failures, unsupported SRE/EOI/revision capabilities, duplicate init, PPI/SPI routing, trigger/priority/pending operations, invalid and spurious acknowledgements, unknown valid interrupts, EOI behavior, and the timer-to-registry-to-scheduler simulation.

The host test script does not launch QEMU. The Phase 2, Phase 3, and Phase 4 build scripts also run their existing parser, scheduler, allocator, and VFS hosted controls.

## ARM64 freestanding build

`scripts/build-aarch64-gicv3.ps1` builds the Phase 4 common-kernel profile with the repository LLVM toolchain. It compiles `gicv3_controller.cpp`, links `kernel.elf`, and checks that the production object and ELF exist. Phase 2 and Phase 3 were also built separately to cover the preserved GICv2 path and the standalone scheduler path. Phase 5 through Phase 9 cross-builds also completed, including their hosted controls; the Phase 7 input host controls passed. Their build lists now include the common native-architecture helper that the existing NativeElf source already requires.

The Phase 12 wrapper compiled and linked its ARM64 kernel and passed the Phase 10 common host controls. Its additional Developer Studio build step then failed in `document_activation_abi_test.cpp` because that external project expects `gx_host_calls.get_document_activation_path`, which is absent from its current ABI definition. No Developer Studio sources were changed for this task. The Phase 12 application packaging gate therefore remains unverified by this run.

The generic page-table pool is bounded. It can map sparse high physical MMIO addresses within the configured 40-bit PA range, but setup still fails if the platform's RAM/MMIO ranges require more than 520 tables. This is a current memory-map limit, not a GIC register-address guess.

## Guest qualification and QAC

No QAC grant was available, so no QEMU process was started. `scripts/test-aarch64-gicv3-qemu.ps1` fails before guest launch unless given an unexpired exclusive grant file with these fields:

```json
{
  "issuer": "guideXOS-QAC",
  "status": "granted",
  "resource": "qemu-system-aarch64",
  "purpose": "AARCH64-GIC3-1",
  "exclusive": true,
  "reservation_id": "...",
  "expires_utc": "..."
}
```

This checkout does not contain a QAC issuer/provider that can create that grant. A file must come from the actual QAC coordination process; a manually fabricated file is not authorization. After a genuine grant is available, the script checks the installed QEMU version, `virt` machine, and `cortex-a57` CPU support, then runs three fresh UEFI boots with `gic-version=3`. It requires DTB GICv3 discovery, the GICv3 initialization marker, at least 1,000 timer-driven scheduler preemptions per boot, the guest pass marker, and zero unexpected IRQs/exceptions.

The historical standalone Phase 3 timeout remains a separate baseline item. It was not re-run or reclassified in this hosted/build-only window.

## Raspberry Pi 4 and Galaxy Tab A8

The GICv2 parser and GICD/GICC path remain selected for the Raspberry Pi 4. The Phase 3 and Phase 4 freestanding builds compile that path, and the framebuffer MMU path still maps the GICv2 CPU interface. No Pi guest or physical-board test was run during this task.

For the Galaxy Tab A8, the DTB-driven GICv3 and boot-CPU Redistributor design applies, and the previously observed physical timer INTID 30 fits the PPI path. The Distributor and Redistributor physical addresses remain unknown. No assumed UMS512 addresses, tablet access, or removable-device access were used.

## Next qualification step

Obtain an exclusive QAC reservation for `AARCH64-GIC3-1`, then run the separate GICv3 QEMU script. Guest qualification must establish actual GICv3 DTB discovery, Distributor and boot CPU Redistributor setup, ICC system-register operation, physical timer PPI delivery, common IRQ dispatch, scheduler preemption, clean exception return, and zero unexpected IRQ/exception counts. Until those guest runs pass, the outcome remains `GICV3_HOSTED_READY_GUEST_PENDING`.
