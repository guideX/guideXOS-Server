#pragma once

#include <stdint.h>

uint8_t phase2_timer_init(uint32_t irq);
void phase2_timer_disable();
void phase2_timer_note_irq();
uint64_t phase2_timer_frequency();
uint32_t phase2_timer_irq();
uint32_t phase2_timer_count();
uint32_t phase2_timer_control();
uint32_t phase2_timer_value();
