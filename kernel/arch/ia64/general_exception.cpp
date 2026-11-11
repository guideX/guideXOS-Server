// Strict validation and reporting for the single early-exec General Exception.
#include "include/arch/general_exception.h"
#include "include/arch/ski_console.h"

namespace kernel {
namespace arch {
namespace ia64 {
namespace {

static const uint64_t PSR_IC = 1ull << 13;
static const uint64_t PSR_I = 1ull << 14;
static const uint64_t IPSR_CPL_MASK = 3ull << 32;
static const uint64_t IPSR_RI_MASK = 3ull << 41;
static const uint64_t IPSR_BN = 1ull << 44;
static const uint64_t IPSR_VM = 1ull << 46;

enum GeneralExceptionError : uint64_t {
    BAD_FRAME_VALID = 1ull << 0,
    BAD_SCHEMA = 1ull << 1,
    BAD_VECTOR = 1ull << 2,
    BAD_SUBTYPE = 1ull << 3,
    BAD_SITE = 1ull << 4,
    BAD_SLOT = 1ull << 5,
    BAD_IPSR = 1ull << 6,
    BAD_IFS = 1ull << 7,
    BAD_ISR = 1ull << 8,
    BAD_HANDLER = 1ull << 9,
    BAD_RSE = 1ull << 10,
    BAD_RETURN_TARGET = 1ull << 11,
    BAD_COMPLETION = 1ull << 12,
    BAD_POST_RFI_PSR = 1ull << 13,
    BAD_POST_RFI_IVA = 1ull << 14,
    BAD_POST_RFI_UNAT = 1ull << 15,
    BAD_STATIC_GR = 1ull << 16,
    BAD_CONTINUATION = 1ull << 17,
};

bool record_is_zero(const volatile GeneralExceptionRecord& record)
{
    return record.valid_marker == 0 && record.schema_version == 0 &&
        record.record_size == 0 && record.vector_offset == 0 &&
        record.fault_subtype == 0 && record.saved_iip == 0 &&
        record.saved_slot == 0 && record.saved_ipsr == 0 &&
        record.saved_ifs == 0 && record.saved_ifs_valid == 0 &&
        record.saved_isr == 0 && record.handler_entry_psr == 0 &&
        record.handler_entry_rsc == 0 && record.handler_entry_bsp == 0 &&
        record.saved_predicates == 0 && record.fault_site == 0 &&
        record.return_target == 0 && record.handler_entry_status == 0 &&
        record.completion_marker == 0 && record.continuation_count == 0 &&
        record.return_marker == 0 && record.post_rfi_psr == 0 &&
        record.post_rfi_rsc == 0 && record.post_rfi_bsp == 0 &&
        record.post_rfi_iva == 0 && record.pre_fault_bsp == 0 &&
        record.pre_fault_rsc == 0 && record.pre_fault_unat == 0 &&
        record.post_rfi_unat == 0 && record.pre_fault_gr_checksum == 0 &&
        record.post_rfi_gr_checksum == 0 && record.post_rfi_valid_marker == 0;
}

uint64_t validate_record(const volatile GeneralExceptionRecord& record,
                         int test_result)
{
    uint64_t errors = 0;
    if (record.valid_marker != GENERAL_EXCEPTION_VALID_MARKER)
        errors |= BAD_FRAME_VALID;
    if (record.schema_version != GENERAL_EXCEPTION_SCHEMA_VERSION ||
        record.record_size != GENERAL_EXCEPTION_RECORD_SIZE)
        errors |= BAD_SCHEMA;
    if (record.vector_offset != 0x5400) errors |= BAD_VECTOR;
    if (record.fault_subtype != GENERAL_EXCEPTION_ILLEGAL_OPERATION)
        errors |= BAD_SUBTYPE;
    if (record.saved_iip != reinterpret_cast<uint64_t>(ia64_general_exception_fault_site) ||
        record.fault_site != reinterpret_cast<uint64_t>(ia64_general_exception_fault_site) ||
        (record.saved_iip & 0xf) != 0)
        errors |= BAD_SITE;
    if (record.saved_slot != 0 ||
        (record.saved_ipsr & IPSR_RI_MASK) != 0 ||
        ((record.saved_isr >> 41) & 3ull) != 0)
        errors |= BAD_SLOT;

    const uint64_t ipsr = record.saved_ipsr;
    if ((ipsr & PSR_IC) == 0 || (ipsr & PSR_I) != 0 ||
        (ipsr & IPSR_CPL_MASK) != 0 || (ipsr & IPSR_VM) != 0 ||
        (ipsr & IPSR_BN) != 0)
        errors |= BAD_IPSR;
    if (record.saved_ifs != 0 || record.saved_ifs_valid != 0)
        errors |= BAD_IFS;
    if (record.saved_isr != 0) errors |= BAD_ISR;

    if ((record.handler_entry_psr & (PSR_I | PSR_IC)) != 0 ||
        record.handler_entry_rsc != 3 ||
        record.handler_entry_status != GENERAL_EXCEPTION_HANDLER_VALID ||
        record.saved_predicates != 0x401)
        errors |= BAD_HANDLER;

    const uint64_t rse_base = reinterpret_cast<uint64_t>(rse_backing_store_base);
    const uint64_t rse_top = reinterpret_cast<uint64_t>(rse_backing_store_top);
    if (record.handler_entry_bsp < rse_base || record.handler_entry_bsp >= rse_top ||
        record.pre_fault_bsp != record.handler_entry_bsp ||
        record.post_rfi_bsp != record.handler_entry_bsp ||
        record.pre_fault_rsc != 3 || record.post_rfi_rsc != 3)
        errors |= BAD_RSE;

    if (record.return_target != reinterpret_cast<uint64_t>(ia64_general_exception_continuation))
        errors |= BAD_RETURN_TARGET;
    if (record.completion_marker != GENERAL_EXCEPTION_COMPLETION ||
        record.return_marker != GENERAL_EXCEPTION_RETURN ||
        record.continuation_count != 1 || test_result != 0)
        errors |= BAD_COMPLETION;

    if (record.post_rfi_psr != 0x2000 ||
        (record.post_rfi_psr & (PSR_I | PSR_IC)) != PSR_IC)
        errors |= BAD_POST_RFI_PSR;
    if (record.post_rfi_iva != reinterpret_cast<uint64_t>(ia64_ivt))
        errors |= BAD_POST_RFI_IVA;
    if (record.pre_fault_unat != record.post_rfi_unat)
        errors |= BAD_POST_RFI_UNAT;
    if (record.pre_fault_gr_checksum != record.post_rfi_gr_checksum)
        errors |= BAD_STATIC_GR;
    if (record.post_rfi_valid_marker != GENERAL_EXCEPTION_POST_RFI_VALID)
        errors |= BAD_CONTINUATION;
    return errors;
}

void report_record(const volatile GeneralExceptionRecord& record)
{
    ski_console::puts("IA64_GENERAL_EXCEPTION_RECORD");
    ski_console::puts(" valid="); ski_console::put_hex(record.valid_marker);
    ski_console::puts(" version="); ski_console::put_hex(record.schema_version);
    ski_console::puts(" size="); ski_console::put_hex(record.record_size);
    ski_console::puts(" vector="); ski_console::put_hex(record.vector_offset);
    ski_console::puts(" subtype="); ski_console::put_hex(record.fault_subtype);
    ski_console::puts(" IIP="); ski_console::put_hex(record.saved_iip);
    ski_console::puts(" slot="); ski_console::put_hex(record.saved_slot);
    ski_console::puts(" IPSR="); ski_console::put_hex(record.saved_ipsr);
    ski_console::puts(" IFS="); ski_console::put_hex(record.saved_ifs);
    ski_console::puts(" IFS.valid="); ski_console::put_hex(record.saved_ifs_valid);
    ski_console::puts(" ISR="); ski_console::put_hex(record.saved_isr);
    ski_console::puts(" handler.PSR="); ski_console::put_hex(record.handler_entry_psr);
    ski_console::puts(" handler.RSC="); ski_console::put_hex(record.handler_entry_rsc);
    ski_console::puts(" handler.BSP="); ski_console::put_hex(record.handler_entry_bsp);
    ski_console::puts(" PR="); ski_console::put_hex(record.saved_predicates);
    ski_console::puts(" site="); ski_console::put_hex(record.fault_site);
    ski_console::puts(" target="); ski_console::put_hex(record.return_target);
    ski_console::puts(" handler.status="); ski_console::put_hex(record.handler_entry_status);
    ski_console::puts(" pre.BSP="); ski_console::put_hex(record.pre_fault_bsp);
    ski_console::puts(" pre.RSC="); ski_console::put_hex(record.pre_fault_rsc);
    ski_console::puts(" pre.UNAT="); ski_console::put_hex(record.pre_fault_unat);
    ski_console::puts(" pre.GR="); ski_console::put_hex(record.pre_fault_gr_checksum);
    ski_console::puts(" completion="); ski_console::put_hex(record.completion_marker);
    ski_console::puts(" count="); ski_console::put_hex(record.continuation_count);
    ski_console::puts(" return="); ski_console::put_hex(record.return_marker);
    ski_console::puts(" post.PSR="); ski_console::put_hex(record.post_rfi_psr);
    ski_console::puts(" post.RSC="); ski_console::put_hex(record.post_rfi_rsc);
    ski_console::puts(" post.BSP="); ski_console::put_hex(record.post_rfi_bsp);
    ski_console::puts(" post.IVA="); ski_console::put_hex(record.post_rfi_iva);
    ski_console::puts(" post.UNAT="); ski_console::put_hex(record.post_rfi_unat);
    ski_console::puts(" post.GR="); ski_console::put_hex(record.post_rfi_gr_checksum);
    ski_console::puts(" post.valid="); ski_console::put_hex(record.post_rfi_valid_marker);
    ski_console::puts("\r\n");
}

} // namespace

bool run_general_exception_qualification()
{
    if (!record_is_zero(ia64_general_exception_record)) {
        ski_console::puts("IA64_GENERAL_EXCEPTION_RESULT=FAIL reason=stale-record\r\n");
        return false;
    }

    ski_console::puts("IA64_GENERAL_EXCEPTION_PRE_FAULT vector=0x5400 subtype=illegal-operation site=");
    ski_console::put_hex(reinterpret_cast<uint64_t>(ia64_general_exception_fault_site));
    ski_console::puts(" target=");
    ski_console::put_hex(reinterpret_cast<uint64_t>(ia64_general_exception_continuation));
    ski_console::puts(" expected_faults=1\r\n");

    const int test_result = ia64_general_exception_test();
    const uint64_t errors = validate_record(ia64_general_exception_record, test_result);
    report_record(ia64_general_exception_record);
    ski_console::puts("IA64_GENERAL_EXCEPTION_VALIDATION errors=");
    ski_console::put_hex(errors);
    ski_console::puts(" test=");
    ski_console::put_hex(static_cast<uint64_t>(test_result));
    ski_console::puts("\r\n");
    ski_console::puts("IA64_GENERAL_EXCEPTION_POST_RETURN target=");
    ski_console::put_hex(ia64_general_exception_record.return_target);
    ski_console::puts(" count=");
    ski_console::put_hex(ia64_general_exception_record.continuation_count);
    ski_console::puts(" PSR=");
    ski_console::put_hex(ia64_general_exception_record.post_rfi_psr);
    ski_console::puts("\r\n");
    if (test_result != 0 || errors != 0) {
        ski_console::puts("IA64_GENERAL_EXCEPTION_RESULT=FAIL\r\n");
        return false;
    }
    ski_console::puts("IA64_GENERAL_EXCEPTION_RESULT=PASS\r\n");
    return true;
}

} // namespace ia64
} // namespace arch
} // namespace kernel
