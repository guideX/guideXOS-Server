#pragma once

#include <stdint.h>

#include "../../../aarch64/phase2/phase2_platform.h"

uint8_t phase2_gic_init(const gxos_aarch64_phase2_platform* platform, uint32_t timer_irq);
uint32_t phase2_gic_acknowledge();
void phase2_gic_complete(uint32_t acknowledgement);
uint32_t phase2_gic_irq_id(uint32_t acknowledgement);
uint32_t phase2_gic_distributor_control();
uint32_t phase2_gic_enable0();
uint32_t phase2_gic_group0();
uint32_t phase2_gic_cpu_control();
uint32_t phase2_gic_priority_mask();
