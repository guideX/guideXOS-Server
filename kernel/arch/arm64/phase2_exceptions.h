#pragma once

#include <stdint.h>

uint8_t phase2_exception_self_test();
uint64_t phase2_exception_self_test_esr();
uint64_t phase2_exception_self_test_elr();
uint64_t phase2_exception_self_test_expected_elr();
uint64_t phase2_exception_vectors_address();
