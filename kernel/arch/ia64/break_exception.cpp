// Validation and bounded post-return reporting for the early-exec test.
#include "include/arch/break_exception.h"
#include "include/arch/ski_console.h"

namespace kernel {
namespace arch {
namespace ia64 {
namespace {

static const uint64_t IPSR_RI_MASK = 3ull << 41;
static const uint64_t IPSR_IC = 1ull << 13;
static const uint64_t IPSR_I = 1ull << 14;
static const uint64_t IPSR_CPL_MASK = 3ull << 32;
static const uint64_t IPSR_BN = 1ull << 44;
static const uint64_t IPSR_VM = 1ull << 46;
static const uint64_t ISR_EI_MASK = 7ull << 41;
static const uint64_t ISR_NI = 1ull << 38;
static const uint64_t PSR_IC = 1ull << 13;
static const uint64_t PSR_I = 1ull << 14;

enum RecordError : uint64_t {
    RECORD_BAD_VALID       = 1ull << 0,
    RECORD_BAD_SCHEMA      = 1ull << 1,
    RECORD_BAD_VECTOR      = 1ull << 2,
    RECORD_BAD_IMMEDIATE   = 1ull << 3,
    RECORD_BAD_IIP         = 1ull << 4,
    RECORD_BAD_SLOT        = 1ull << 5,
    RECORD_BAD_IPSR        = 1ull << 6,
    RECORD_BAD_IFS         = 1ull << 7,
    RECORD_BAD_ISR         = 1ull << 8,
    RECORD_BAD_HANDLER     = 1ull << 9,
    RECORD_BAD_RSE         = 1ull << 10,
    RECORD_BAD_COMPLETION  = 1ull << 11,
    RECORD_BAD_RETURN      = 1ull << 12,
    RECORD_BAD_PREDICATES  = 1ull << 13,
};

bool is_zero(const volatile BreakExceptionRecord& record)
{
    return record.valid_marker == 0 && record.schema_version == 0 &&
        record.vector_offset == 0 && record.break_immediate == 0 &&
        record.saved_iip == 0 && record.saved_slot == 0 &&
        record.saved_ipsr == 0 && record.saved_ifs_valid == 0 &&
        record.saved_isr == 0 && record.handler_entry_psr == 0 &&
        record.handler_entry_rsc == 0 && record.handler_entry_bsp == 0 &&
        record.completion_marker == 0 && record.return_marker == 0 &&
        record.continuation_count == 0 && record.saved_predicates == 0;
}

uint64_t validate_one(const volatile BreakExceptionRecord& record,
                      uint64_t expected_immediate, uint64_t expected_iip,
                      uint64_t expected_slot, uint64_t expected_completion,
                      uint64_t expected_return)
{
    uint64_t errors = 0;
    if (record.valid_marker != BREAK_EXCEPTION_VALID_MARKER) errors |= RECORD_BAD_VALID;
    if (record.schema_version != BREAK_EXCEPTION_SCHEMA_VERSION) errors |= RECORD_BAD_SCHEMA;
    if (record.vector_offset != 0x2c00) errors |= RECORD_BAD_VECTOR;
    if (record.break_immediate != expected_immediate) errors |= RECORD_BAD_IMMEDIATE;
    if (record.saved_iip != expected_iip || (record.saved_iip & 0xf) != 0)
        errors |= RECORD_BAD_IIP;
    if (record.saved_slot != expected_slot) errors |= RECORD_BAD_SLOT;

    const uint64_t ipsr = record.saved_ipsr;
    const uint64_t expected_ri = expected_slot << 41;
    if ((ipsr & IPSR_RI_MASK) != expected_ri || (ipsr & IPSR_IC) == 0 ||
        (ipsr & IPSR_I) != 0 || (ipsr & IPSR_BN) == 0 ||
        (ipsr & IPSR_CPL_MASK) != 0 || (ipsr & IPSR_VM) != 0)
        errors |= RECORD_BAD_IPSR;
    if (record.saved_ifs_valid != 0) errors |= RECORD_BAD_IFS;

    const uint64_t isr_ei = (record.saved_isr & ISR_EI_MASK) >> 41;
    if (isr_ei != expected_slot || (record.saved_isr & ISR_NI) != 0 ||
        (record.saved_isr & 0xffffull) != 0)
        errors |= RECORD_BAD_ISR;

    if ((record.handler_entry_psr & (PSR_I | PSR_IC)) != 0 ||
        record.handler_entry_rsc != 3)
        errors |= RECORD_BAD_HANDLER;

    const uint64_t bsp = record.handler_entry_bsp;
    const uint64_t rse_begin = reinterpret_cast<uint64_t>(rse_backing_store_base);
    const uint64_t rse_end = reinterpret_cast<uint64_t>(rse_backing_store_top);
    if (bsp < rse_begin || bsp > rse_end) errors |= RECORD_BAD_RSE;

    if (record.completion_marker != expected_completion) errors |= RECORD_BAD_COMPLETION;
    if (record.return_marker != expected_return || record.continuation_count != 1)
        errors |= RECORD_BAD_RETURN;
    if (record.saved_predicates != 0x401ull)
        errors |= RECORD_BAD_PREDICATES;
    return errors;
}

void report_record(const char* name, const volatile BreakExceptionRecord& record)
{
    ski_console::puts("IA64_BREAK_RECORD ");
    ski_console::puts(name);
    ski_console::puts(" valid="); ski_console::put_hex(record.valid_marker);
    ski_console::puts(" vector="); ski_console::put_hex(record.vector_offset);
    ski_console::puts(" immediate="); ski_console::put_hex(record.break_immediate);
    ski_console::puts(" IIP="); ski_console::put_hex(record.saved_iip);
    ski_console::puts(" slot="); ski_console::put_hex(record.saved_slot);
    ski_console::puts(" IPSR="); ski_console::put_hex(record.saved_ipsr);
    ski_console::puts(" IFS.valid="); ski_console::put_hex(record.saved_ifs_valid);
    ski_console::puts(" ISR="); ski_console::put_hex(record.saved_isr);
    ski_console::puts(" handler.PSR="); ski_console::put_hex(record.handler_entry_psr);
    ski_console::puts(" RSC="); ski_console::put_hex(record.handler_entry_rsc);
    ski_console::puts(" BSP="); ski_console::put_hex(record.handler_entry_bsp);
    ski_console::puts(" completion="); ski_console::put_hex(record.completion_marker);
    ski_console::puts(" return="); ski_console::put_hex(record.return_marker);
    ski_console::puts(" count="); ski_console::put_hex(record.continuation_count);
    ski_console::puts(" PR="); ski_console::put_hex(record.saved_predicates);
    ski_console::puts("\r\n");
}

} // namespace

uint64_t ia64_break_exception_validate_records()
{
    uint64_t errors = validate_one(
        ia64_break_exception_record_slot1, 0x6a17,
        reinterpret_cast<uint64_t>(ia64_break_slot1_bundle), 1,
        BREAK_EXCEPTION_COMPLETION_SLOT1, BREAK_EXCEPTION_RETURN_SLOT1);
    errors |= validate_one(
        ia64_break_exception_record_slot2, 0x6a18,
        reinterpret_cast<uint64_t>(ia64_break_slot2_bundle), 2,
        BREAK_EXCEPTION_COMPLETION_SLOT2, BREAK_EXCEPTION_RETURN_SLOT2);
    return errors;
}

#if defined(GXOS_IA64_EARLY_EXEC_PROFILE)
bool run_break_exception_qualification()
{
    if (!is_zero(ia64_break_exception_record_slot1) ||
        !is_zero(ia64_break_exception_record_slot2)) {
        ski_console::puts("IA64_BREAK_EXCEPTION_RESULT=FAIL reason=stale-record\r\n");
        return false;
    }

    const int test_result = ia64_break_exception_test();
    const uint64_t errors = ia64_break_exception_validate_records();
    report_record("slot1", ia64_break_exception_record_slot1);
    report_record("slot2", ia64_break_exception_record_slot2);
    ski_console::puts("IA64_BREAK_EXCEPTION_VALIDATION errors=");
    ski_console::put_hex(errors);
    ski_console::puts(" test=");
    ski_console::put_hex(static_cast<uint64_t>(test_result));
    ski_console::puts("\r\n");
    if (test_result != 0 || errors != 0) {
        ski_console::puts("IA64_BREAK_EXCEPTION_RESULT=FAIL\r\n");
        return false;
    }
    ski_console::puts("IA64_BREAK_EXCEPTION_RESULT=PASS\r\n");
    return true;
}
#endif

} // namespace ia64
} // namespace arch
} // namespace kernel
