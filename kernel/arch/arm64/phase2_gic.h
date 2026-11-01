#pragma once

#include <stdint.h>

#include "../../../aarch64/phase2/phase2_platform.h"

enum class phase2_gic_controller_kind : uint32_t {
    Unsupported = GXOS_AARCH64_GIC_UNSUPPORTED,
    GicV2 = GXOS_AARCH64_GIC_V2,
    GicV3 = GXOS_AARCH64_GIC_V3
};

phase2_gic_controller_kind phase2_gic_select_controller(
    const gxos_aarch64_phase2_platform* platform);
uint8_t phase2_gic_init(const gxos_aarch64_phase2_platform* platform, uint32_t timer_irq);
uint8_t phase2_gic_enable_irq(uint32_t irq);
uint8_t phase2_gic_disable_irq(uint32_t irq);
uint32_t phase2_gic_acknowledge();
void phase2_gic_complete(uint32_t acknowledgement);
uint32_t phase2_gic_irq_id(uint32_t acknowledgement);
uint8_t phase2_gic_ack_valid(uint32_t acknowledgement);
uint32_t phase2_gic_version();
uint32_t phase2_gic_last_error();
uint32_t phase2_gic_distributor_control();
uint32_t phase2_gic_enable0();
uint32_t phase2_gic_group0();
uint32_t phase2_gic_cpu_control();
uint32_t phase2_gic_priority_mask();

struct phase2_gicv3_diagnostics {
    uint64_t distributor_base;
    uint64_t redistributor_base;
    uint64_t sgi_base;
    uint64_t boot_affinity;
    uint64_t mpidr_el1;
    uint64_t icc_sre_el1;
    uint64_t icc_pmr_el1;
    uint64_t icc_bpr1_el1;
    uint64_t icc_igrpen1_el1;
    uint64_t icc_ctlr_el1;
    uint64_t acknowledge_count;
    uint64_t eoi_count;
    uint64_t invalid_acknowledgements;
    uint64_t completion_errors;
    uint32_t timer_interrupt;
    uint32_t wait_limit;
    uint32_t last_acknowledgement;
    uint32_t distributor_ctlr;
    uint32_t redistributor_ctlr;
    uint32_t redistributor_waker;
    uint32_t timer_group;
    uint32_t timer_enable;
    uint32_t timer_config;
    uint8_t timer_priority;
    uint8_t initialized;
    uint8_t eoimode_split;
    uint8_t acknowledgement_pending;
};

uint8_t phase2_gicv3_get_diagnostics(phase2_gicv3_diagnostics* diagnostics);
