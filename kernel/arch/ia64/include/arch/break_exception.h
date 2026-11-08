#pragma once

#include <stddef.h>
#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace ia64 {

static const uint64_t BREAK_EXCEPTION_SCHEMA_VERSION = 1;
static const uint64_t BREAK_EXCEPTION_VALID_MARKER = 0xBEEFC0DE4B56414Cull;
static const uint64_t BREAK_EXCEPTION_COMPLETION_SLOT1 = 0xBEEFC0DE4B000001ull;
static const uint64_t BREAK_EXCEPTION_COMPLETION_SLOT2 = 0xBEEFC0DE4B000002ull;
static const uint64_t BREAK_EXCEPTION_RETURN_SLOT1 = 0xBEEFC0DE4B100001ull;
static const uint64_t BREAK_EXCEPTION_RETURN_SLOT2 = 0xBEEFC0DE4B100002ull;

struct alignas(16) BreakExceptionRecord {
    uint64_t valid_marker;
    uint64_t schema_version;
    uint64_t vector_offset;
    uint64_t break_immediate;
    uint64_t saved_iip;
    uint64_t saved_slot;
    uint64_t saved_ipsr;
    uint64_t saved_ifs_valid;
    uint64_t saved_isr;
    uint64_t handler_entry_psr;
    uint64_t handler_entry_rsc;
    uint64_t handler_entry_bsp;
    uint64_t completion_marker;
    uint64_t return_marker;
    uint64_t continuation_count;
    uint64_t saved_predicates;
};

static_assert(sizeof(BreakExceptionRecord) == 128, "break exception record layout changed");
static_assert(offsetof(BreakExceptionRecord, valid_marker) == 0, "record validity offset changed");
static_assert(offsetof(BreakExceptionRecord, schema_version) == 8, "record version offset changed");
static_assert(offsetof(BreakExceptionRecord, vector_offset) == 16, "record vector offset changed");
static_assert(offsetof(BreakExceptionRecord, break_immediate) == 24, "record immediate offset changed");
static_assert(offsetof(BreakExceptionRecord, saved_iip) == 32, "record IIP offset changed");
static_assert(offsetof(BreakExceptionRecord, saved_slot) == 40, "record slot offset changed");
static_assert(offsetof(BreakExceptionRecord, saved_ipsr) == 48, "record IPSR offset changed");
static_assert(offsetof(BreakExceptionRecord, saved_ifs_valid) == 56, "record IFS offset changed");
static_assert(offsetof(BreakExceptionRecord, saved_isr) == 64, "record ISR offset changed");
static_assert(offsetof(BreakExceptionRecord, handler_entry_psr) == 72, "record handler PSR offset changed");
static_assert(offsetof(BreakExceptionRecord, handler_entry_rsc) == 80, "record handler RSC offset changed");
static_assert(offsetof(BreakExceptionRecord, handler_entry_bsp) == 88, "record handler BSP offset changed");
static_assert(offsetof(BreakExceptionRecord, completion_marker) == 96, "record completion offset changed");
static_assert(offsetof(BreakExceptionRecord, return_marker) == 104, "record continuation offset changed");
static_assert(offsetof(BreakExceptionRecord, continuation_count) == 112, "record count offset changed");
static_assert(offsetof(BreakExceptionRecord, saved_predicates) == 120, "record predicates offset changed");

extern "C" volatile BreakExceptionRecord ia64_break_exception_record_slot1;
extern "C" volatile BreakExceptionRecord ia64_break_exception_record_slot2;
extern "C" char ia64_break_slot1_bundle[];
extern "C" char ia64_break_slot2_bundle[];
extern "C" char rse_backing_store_base[];
extern "C" char rse_backing_store_top[];

extern "C" int ia64_break_exception_test();
uint64_t ia64_break_exception_validate_records();

#if defined(GXOS_IA64_EARLY_EXEC_PROFILE)
bool run_break_exception_qualification();
#endif

} // namespace ia64
} // namespace arch
} // namespace kernel
