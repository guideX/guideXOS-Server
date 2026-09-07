#pragma once

#include <stdint.h>

uint8_t phase3_timer_configure(uint32_t irq, uint32_t interval_us);
void phase3_timer_start();
void phase3_timer_ack_and_rearm();
void phase3_timer_stop();
uint32_t phase3_timer_irq();
uint64_t phase3_timer_frequency();
uint64_t phase3_timer_count();

