#pragma once

#include <stddef.h>
#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace ia64 {

static const uint64_t GENERAL_EXCEPTION_SCHEMA_VERSION = 1;
static const uint64_t GENERAL_EXCEPTION_RECORD_SIZE = 256;
static const uint64_t GENERAL_EXCEPTION_VALID_MARKER = 0xBEEFC0DE47455631ull;
static const uint64_t GENERAL_EXCEPTION_HANDLER_VALID = 0xBEEFC0DE47454831ull;
static const uint64_t GENERAL_EXCEPTION_COMPLETION = 0xBEEFC0DE47454331ull;
static const uint64_t GENERAL_EXCEPTION_RETURN = 0xBEEFC0DE47455231ull;
static const uint64_t GENERAL_EXCEPTION_POST_RFI_VALID = 0xBEEFC0DE47505331ull;
static const uint64_t GENERAL_EXCEPTION_ILLEGAL_OPERATION = 0;

struct alignas(16) GeneralExceptionRecord {
    uint64_t valid_marker;
    uint64_t schema_version;
    uint64_t record_size;
    uint64_t vector_offset;
    uint64_t fault_subtype;
    uint64_t saved_iip;
    uint64_t saved_slot;
    uint64_t saved_ipsr;
    uint64_t saved_ifs;
    uint64_t saved_ifs_valid;
    uint64_t saved_isr;
    uint64_t handler_entry_psr;
    uint64_t handler_entry_rsc;
    uint64_t handler_entry_bsp;
    uint64_t saved_predicates;
    uint64_t fault_site;
    uint64_t return_target;
    uint64_t handler_entry_status;
    uint64_t completion_marker;
    uint64_t continuation_count;
    uint64_t return_marker;
    uint64_t post_rfi_psr;
    uint64_t post_rfi_rsc;
    uint64_t post_rfi_bsp;
    uint64_t post_rfi_iva;
    uint64_t pre_fault_bsp;
    uint64_t pre_fault_rsc;
    uint64_t pre_fault_unat;
    uint64_t post_rfi_unat;
    uint64_t pre_fault_gr_checksum;
    uint64_t post_rfi_gr_checksum;
    uint64_t post_rfi_valid_marker;
};

static_assert(sizeof(GeneralExceptionRecord) == GENERAL_EXCEPTION_RECORD_SIZE,
              "general exception record layout changed");
static_assert(offsetof(GeneralExceptionRecord, vector_offset) == 24,
              "general exception vector offset changed");
static_assert(offsetof(GeneralExceptionRecord, saved_ipsr) == 56,
              "general exception IPSR offset changed");
static_assert(offsetof(GeneralExceptionRecord, saved_ifs) == 64,
              "general exception IFS offset changed");
static_assert(offsetof(GeneralExceptionRecord, saved_isr) == 80,
              "general exception ISR offset changed");
static_assert(offsetof(GeneralExceptionRecord, handler_entry_psr) == 88,
              "general exception entry PSR offset changed");
static_assert(offsetof(GeneralExceptionRecord, handler_entry_rsc) == 96,
              "general exception entry RSC offset changed");
static_assert(offsetof(GeneralExceptionRecord, handler_entry_bsp) == 104,
              "general exception entry BSP offset changed");
static_assert(offsetof(GeneralExceptionRecord, saved_predicates) == 112,
              "general exception predicate offset changed");
static_assert(offsetof(GeneralExceptionRecord, return_target) == 128,
              "general exception return target offset changed");
static_assert(offsetof(GeneralExceptionRecord, post_rfi_psr) == 168,
              "general exception post-RFI PSR offset changed");
static_assert(offsetof(GeneralExceptionRecord, post_rfi_valid_marker) == 248,
              "general exception post-RFI validity offset changed");

extern "C" volatile GeneralExceptionRecord ia64_general_exception_record;
extern "C" char ia64_general_exception_fault_site[];
extern "C" char ia64_general_exception_continuation[];
extern "C" char ia64_ivt[];
extern "C" char rse_backing_store_base[];
extern "C" char rse_backing_store_top[];

extern "C" int ia64_general_exception_test();
bool run_general_exception_qualification();

} // namespace ia64
} // namespace arch
} // namespace kernel
