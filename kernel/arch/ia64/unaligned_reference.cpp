// Validation and reporting for one bounded Ski Unaligned Reference test.
#include "include/arch/unaligned_reference.h"
#include "include/arch/ski_console.h"

namespace kernel {
namespace arch {
namespace ia64 {
namespace {

static const uint64_t PSR_AC = 1ull << 3;
static const uint64_t PSR_IC = 1ull << 13;
static const uint64_t PSR_I = 1ull << 14;
static const uint64_t IPSR_CPL_MASK = 3ull << 32;
static const uint64_t IPSR_RI_MASK = 3ull << 41;
static const uint64_t IPSR_BN = 1ull << 44;
static const uint64_t IPSR_VM = 1ull << 46;
static const uint64_t ISR_READ = 1ull << 34;

enum UnalignedReferenceError : uint64_t {
    BAD_VALIDITY = 1ull << 0,
    BAD_SCHEMA = 1ull << 1,
    BAD_SEQUENCE = 1ull << 2,
    BAD_VECTOR = 1ull << 3,
    BAD_SUBTYPE = 1ull << 4,
    BAD_SITE = 1ull << 5,
    BAD_SLOT = 1ull << 6,
    BAD_IPSR = 1ull << 7,
    BAD_IFS = 1ull << 8,
    BAD_IFA = 1ull << 9,
    BAD_ISR = 1ull << 10,
    BAD_HANDLER = 1ull << 11,
    BAD_RSE = 1ull << 12,
    BAD_BUFFER = 1ull << 13,
    BAD_RETURN = 1ull << 14,
    BAD_COMPLETION = 1ull << 15,
    BAD_POST_RFI = 1ull << 16,
    BAD_CLEANUP = 1ull << 17,
    BAD_REGISTER_STATE = 1ull << 18,
};

bool record_is_zero(const volatile UnalignedReferenceRecord& record)
{
    const volatile uint64_t* words =
        reinterpret_cast<const volatile uint64_t*>(&record);
    for (size_t index = 0; index < UNALIGNED_REFERENCE_RECORD_SIZE / sizeof(uint64_t); ++index) {
        if (words[index] != 0) return false;
    }
    return true;
}

uint64_t validate_record(const volatile UnalignedReferenceRecord& record,
                         int test_result)
{
    uint64_t errors = 0;
    const uint64_t expected_site =
        reinterpret_cast<uint64_t>(ia64_unaligned_reference_fault_site);
    const uint64_t expected_target =
        reinterpret_cast<uint64_t>(ia64_unaligned_reference_continuation);
    const uint64_t buffer_base = reinterpret_cast<uint64_t>(ia64_unaligned_reference_buffer);
    const uint64_t expected_address = buffer_base + 1;
    const uint64_t expected_ifs_valid = (record.saved_ifs >> 63) & 1ull;

    if (record.valid_marker != UNALIGNED_REFERENCE_VALID_MARKER)
        errors |= BAD_VALIDITY;
    if (record.schema_version != UNALIGNED_REFERENCE_SCHEMA_VERSION ||
        record.record_size != UNALIGNED_REFERENCE_RECORD_SIZE)
        errors |= BAD_SCHEMA;
    if (record.sequence != 1) errors |= BAD_SEQUENCE;
    if (record.vector_offset != 0x5a00) errors |= BAD_VECTOR;
    if (record.fault_subtype != UNALIGNED_REFERENCE_DATA_FAULT)
        errors |= BAD_SUBTYPE;
    if (record.fault_site != expected_site || record.saved_iip != expected_site ||
        (record.saved_iip & 0xf) != 0)
        errors |= BAD_SITE;
    if (record.saved_slot != 0 || (record.saved_ipsr & IPSR_RI_MASK) != 0 ||
        ((record.saved_isr >> 41) & 3ull) != record.saved_slot)
        errors |= BAD_SLOT;

    const uint64_t ipsr = record.saved_ipsr;
    if ((ipsr & PSR_IC) == 0 || (ipsr & PSR_I) != 0 || (ipsr & PSR_AC) == 0 ||
        (ipsr & IPSR_CPL_MASK) != 0 || (ipsr & IPSR_BN) != 0 ||
        (ipsr & IPSR_VM) != 0)
        errors |= BAD_IPSR;
    if (record.saved_ifs_valid != expected_ifs_valid || expected_ifs_valid != 0)
        errors |= BAD_IFS;

    const uint64_t rse_base = reinterpret_cast<uint64_t>(rse_backing_store_base);
    const uint64_t rse_top = reinterpret_cast<uint64_t>(rse_backing_store_top);
    const uint64_t address = record.test_address;
    const uint64_t width = record.access_width;
    if (record.saved_ifa != address || address != expected_address ||
        record.buffer_base != buffer_base || record.buffer_size != 16 || width != 8 ||
        (address & (width - 1)) == 0 || address < buffer_base ||
        address + width > buffer_base + record.buffer_size ||
        address + width < address)
        errors |= BAD_BUFFER;
    if (record.fault_site != expected_site || record.saved_ifa != expected_address)
        errors |= BAD_IFA;

    if (record.saved_isr != ISR_READ || record.isr_ed != 0 || record.isr_ei != 0 ||
        record.isr_ni != 0 || record.isr_sp != 0 || record.isr_read != 1 ||
        record.isr_write != 0)
        errors |= BAD_ISR;

    if ((record.handler_entry_psr & (PSR_I | PSR_IC | PSR_AC)) != 0 ||
        record.handler_entry_rsc != 3 || record.saved_predicates != 0x401 ||
        record.handler_entry_status != UNALIGNED_REFERENCE_HANDLER_VALID)
        errors |= BAD_HANDLER;
    if (record.handler_entry_bsp < rse_base || record.handler_entry_bsp >= rse_top ||
        record.pre_fault_bsp != record.handler_entry_bsp ||
        record.post_rfi_bsp != record.handler_entry_bsp ||
        record.pre_fault_rsc != 3 || record.post_rfi_rsc != 3 ||
        record.pre_fault_psr != 0x2000)
        errors |= BAD_RSE;

    if (record.return_target != expected_target ||
        record.return_ipsr != (record.saved_ipsr & ~PSR_AC) ||
        (record.return_ipsr & PSR_AC) != 0 ||
        record.continuation_count != 1 ||
        record.completion_marker != UNALIGNED_REFERENCE_COMPLETION ||
        record.return_marker != UNALIGNED_REFERENCE_RETURN || test_result != 0)
        errors |= BAD_RETURN | BAD_COMPLETION;
    if (record.post_rfi_psr != 0x2000 ||
        (record.post_rfi_psr & (PSR_I | PSR_IC | PSR_AC)) != PSR_IC ||
        record.post_rfi_iva != reinterpret_cast<uint64_t>(ia64_ivt) ||
        record.post_rfi_valid_marker != UNALIGNED_REFERENCE_POST_RFI_VALID)
        errors |= BAD_POST_RFI;
    if (record.cleanup_marker != UNALIGNED_REFERENCE_CLEANUP ||
        (record.post_rfi_psr & PSR_AC) != 0)
        errors |= BAD_CLEANUP;
    if (record.pre_fault_unat != record.post_rfi_unat ||
        record.pre_fault_gr_checksum != record.post_rfi_gr_checksum ||
        record.reserved != 0)
        errors |= BAD_REGISTER_STATE;
    return errors;
}

void report_record(const volatile UnalignedReferenceRecord& record)
{
    ski_console::puts("IA64_UNALIGNED_REFERENCE_RECORD");
    ski_console::puts(" valid="); ski_console::put_hex(record.valid_marker);
    ski_console::puts(" version="); ski_console::put_hex(record.schema_version);
    ski_console::puts(" size="); ski_console::put_hex(record.record_size);
    ski_console::puts(" sequence="); ski_console::put_hex(record.sequence);
    ski_console::puts(" vector="); ski_console::put_hex(record.vector_offset);
    ski_console::puts(" subtype="); ski_console::put_hex(record.fault_subtype);
    ski_console::puts(" site="); ski_console::put_hex(record.fault_site);
    ski_console::puts(" IIP="); ski_console::put_hex(record.saved_iip);
    ski_console::puts(" slot="); ski_console::put_hex(record.saved_slot);
    ski_console::puts(" IPSR="); ski_console::put_hex(record.saved_ipsr);
    ski_console::puts(" IFS="); ski_console::put_hex(record.saved_ifs);
    ski_console::puts(" IFS.valid="); ski_console::put_hex(record.saved_ifs_valid);
    ski_console::puts(" IFA="); ski_console::put_hex(record.saved_ifa);
    ski_console::puts(" ISR="); ski_console::put_hex(record.saved_isr);
    ski_console::puts(" ISR.ed="); ski_console::put_hex(record.isr_ed);
    ski_console::puts(" ISR.ei="); ski_console::put_hex(record.isr_ei);
    ski_console::puts(" ISR.ni="); ski_console::put_hex(record.isr_ni);
    ski_console::puts(" ISR.sp="); ski_console::put_hex(record.isr_sp);
    ski_console::puts(" ISR.r="); ski_console::put_hex(record.isr_read);
    ski_console::puts(" ISR.w="); ski_console::put_hex(record.isr_write);
    ski_console::puts(" handler.PSR="); ski_console::put_hex(record.handler_entry_psr);
    ski_console::puts(" handler.RSC="); ski_console::put_hex(record.handler_entry_rsc);
    ski_console::puts(" handler.BSP="); ski_console::put_hex(record.handler_entry_bsp);
    ski_console::puts(" PR="); ski_console::put_hex(record.saved_predicates);
    ski_console::puts(" pre.PSR="); ski_console::put_hex(record.pre_fault_psr);
    ski_console::puts(" pre.RSC="); ski_console::put_hex(record.pre_fault_rsc);
    ski_console::puts(" pre.BSP="); ski_console::put_hex(record.pre_fault_bsp);
    ski_console::puts(" pre.UNAT="); ski_console::put_hex(record.pre_fault_unat);
    ski_console::puts(" address="); ski_console::put_hex(record.test_address);
    ski_console::puts(" buffer="); ski_console::put_hex(record.buffer_base);
    ski_console::puts(" buffer.size="); ski_console::put_hex(record.buffer_size);
    ski_console::puts(" width="); ski_console::put_hex(record.access_width);
    ski_console::puts(" target="); ski_console::put_hex(record.return_target);
    ski_console::puts(" return.IPSR="); ski_console::put_hex(record.return_ipsr);
    ski_console::puts(" handler.status="); ski_console::put_hex(record.handler_entry_status);
    ski_console::puts(" completion="); ski_console::put_hex(record.completion_marker);
    ski_console::puts(" count="); ski_console::put_hex(record.continuation_count);
    ski_console::puts(" return="); ski_console::put_hex(record.return_marker);
    ski_console::puts(" post.PSR="); ski_console::put_hex(record.post_rfi_psr);
    ski_console::puts(" post.RSC="); ski_console::put_hex(record.post_rfi_rsc);
    ski_console::puts(" post.BSP="); ski_console::put_hex(record.post_rfi_bsp);
    ski_console::puts(" post.IVA="); ski_console::put_hex(record.post_rfi_iva);
    ski_console::puts(" post.UNAT="); ski_console::put_hex(record.post_rfi_unat);
    ski_console::puts(" pre.GR="); ski_console::put_hex(record.pre_fault_gr_checksum);
    ski_console::puts(" post.GR="); ski_console::put_hex(record.post_rfi_gr_checksum);
    ski_console::puts(" cleanup="); ski_console::put_hex(record.cleanup_marker);
    ski_console::puts(" post.valid="); ski_console::put_hex(record.post_rfi_valid_marker);
    ski_console::puts(" reserved="); ski_console::put_hex(record.reserved);
    ski_console::puts("\r\n");
}

} // namespace

bool run_unaligned_reference_qualification()
{
    if (!record_is_zero(ia64_unaligned_reference_record)) {
        ski_console::puts("IA64_UNALIGNED_REFERENCE_RESULT=FAIL reason=stale-record\r\n");
        return false;
    }

    ski_console::puts("IA64_UNALIGNED_REFERENCE_PRE_FAULT vector=0x5a00 subtype=unaligned-data-reference site=");
    ski_console::put_hex(reinterpret_cast<uint64_t>(ia64_unaligned_reference_fault_site));
    ski_console::puts(" address=");
    ski_console::put_hex(reinterpret_cast<uint64_t>(ia64_unaligned_reference_buffer) + 1);
    ski_console::puts(" width=0x8 target=");
    ski_console::put_hex(reinterpret_cast<uint64_t>(ia64_unaligned_reference_continuation));
    ski_console::puts(" expected_faults=1\r\n");

    const int test_result = ia64_unaligned_reference_test();
    const uint64_t errors = validate_record(ia64_unaligned_reference_record, test_result);
    report_record(ia64_unaligned_reference_record);
    ski_console::puts("IA64_UNALIGNED_REFERENCE_VALIDATION errors=");
    ski_console::put_hex(errors);
    ski_console::puts(" test=");
    ski_console::put_hex(static_cast<uint64_t>(test_result));
    ski_console::puts("\r\n");
    ski_console::puts("IA64_UNALIGNED_REFERENCE_POST_RETURN target=");
    ski_console::put_hex(ia64_unaligned_reference_record.return_target);
    ski_console::puts(" count=");
    ski_console::put_hex(ia64_unaligned_reference_record.continuation_count);
    ski_console::puts(" PSR=");
    ski_console::put_hex(ia64_unaligned_reference_record.post_rfi_psr);
    ski_console::puts(" cleanup=");
    ski_console::put_hex(ia64_unaligned_reference_record.cleanup_marker);
    ski_console::puts("\r\n");
    if (test_result != 0 || errors != 0) {
        ski_console::puts("IA64_UNALIGNED_REFERENCE_RESULT=FAIL\r\n");
        return false;
    }
    ski_console::puts("IA64_UNALIGNED_REFERENCE_RESULT=PASS\r\n");
    return true;
}

} // namespace ia64
} // namespace arch
} // namespace kernel
