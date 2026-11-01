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
