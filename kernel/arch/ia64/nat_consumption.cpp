#include "include/arch/nat_consumption.h"
#include "include/arch/ski_console.h"

namespace kernel {
namespace arch {
namespace ia64 {
namespace {

static const uint64_t ISR_REGISTER_NAT_LD8_SLOT0_SKI = 0x0000000400000010ull;
static const uint64_t PSR_I = 1ull << 14;
static const uint64_t PSR_IC = 1ull << 13;
static const uint64_t PSR_AC = 1ull << 3;
static const uint64_t PSR_ED = 1ull << 43;
static const uint64_t PSR_RI_MASK = 3ull << 41;

uint64_t validate_record(const volatile NatConsumptionRecord& record, int test_result)
{
    uint64_t errors = 0;
    const uint64_t expected_site = reinterpret_cast<uint64_t>(ia64_nat_consumption_fault_site);
    const uint64_t expected_target = reinterpret_cast<uint64_t>(ia64_nat_consumption_continuation);
    const uint64_t expected_spec_site = reinterpret_cast<uint64_t>(ia64_nat_consumption_spec_load_site);
    const uint64_t expected_initial = reinterpret_cast<uint64_t>(ia64_nat_consumption_target);
    const uint64_t rse_base = reinterpret_cast<uint64_t>(rse_backing_store_base);
    const uint64_t rse_top = reinterpret_cast<uint64_t>(rse_backing_store_top);

    if (record.valid_marker != NAT_CONSUMPTION_VALID_MARKER ||
        record.schema_version != NAT_CONSUMPTION_SCHEMA_VERSION ||
        record.record_size != NAT_CONSUMPTION_RECORD_SIZE || record.sequence != 1)
        errors |= 1ull << 0;
    if (record.vector_offset != 0x5600 ||
        record.fault_subtype != NAT_CONSUMPTION_REGISTER_FAULT ||
        record.fault_site != expected_site || record.saved_iip != expected_site ||
        record.saved_slot != 0 || ((record.saved_ipsr >> 41) & 3) != record.saved_slot ||
        (record.saved_ipsr & ~(PSR_ED | PSR_RI_MASK)) !=
            (record.pre_fault_psr & ~(PSR_ED | PSR_RI_MASK)) ||
        record.return_target != expected_target || record.speculative_load_site != expected_spec_site)
        errors |= 1ull << 1;

    if (record.saved_ifs_valid != 0 || ((record.saved_ifs >> 63) & 1) != 0 ||
        record.ifa_architecturally_valid != 0)
        errors |= 1ull << 2;

    if (record.saved_isr != ISR_REGISTER_NAT_LD8_SLOT0_SKI || record.isr_ed != 0 ||
        record.isr_ei != 0 || record.isr_so != 0 || record.isr_ni != 0 ||
        record.isr_ir != 0 || record.isr_rs != 0 || record.isr_sp != 0 ||
        record.isr_na != 0 || record.isr_read != 1 || record.isr_write != 0 ||
        record.isr_execute != 0)
        errors |= 1ull << 3;

    if ((record.handler_entry_psr & (PSR_I | PSR_IC | PSR_AC)) != 0 ||
        record.handler_entry_rsc != 3 || record.saved_predicates != 0x401 ||
        record.handler_status != NAT_CONSUMPTION_HANDLER_VALID ||
        record.handler_entry_bsp < rse_base || record.handler_entry_bsp >= rse_top ||
        record.pre_fault_bsp != record.handler_entry_bsp ||
        record.post_rfi_bsp != record.handler_entry_bsp || record.pre_fault_rsc != 3 ||
        record.post_rfi_rsc != 3 || record.pre_fault_psr != 0x2000 ||
        (record.speculative_load_psr & PSR_ED) == 0 ||
        (record.speculative_load_psr & (PSR_I | PSR_IC)) != PSR_IC ||
        (record.speculative_load_psr & PSR_RI_MASK) != 0 ||
        (record.speculative_load_psr & ~(PSR_ED | PSR_RI_MASK)) !=
            (record.pre_fault_psr & ~(PSR_ED | PSR_RI_MASK)) ||
        (record.post_speculative_load_psr & PSR_ED) != 0)
        errors |= 1ull << 4;

    if (record.source_register != 8 || record.destination_register != 9 ||
        record.source_initial_value != expected_initial || record.source_nat_before_fault != 1 ||
        record.source_nat_at_handler != 1 || record.source_nat_after_rfi != 1 ||
        record.source_nat_after_clear != 0 || record.saved_iib0 != 0 ||
        record.saved_iib1 != 0 || record.saved_iib0_available != 0 ||
        record.saved_iib1_available != 0)
        errors |= 1ull << 5;

    if (record.return_ipsr != (record.saved_ipsr & ~PSR_ED) ||
        (record.return_ipsr & PSR_ED) != 0 || record.continuation_count != 1 ||
        record.completion_marker != NAT_CONSUMPTION_COMPLETION ||
        record.return_marker != NAT_CONSUMPTION_RETURN || test_result != 0 ||
        record.test_result != 0)
        errors |= 1ull << 6;
    if (record.post_rfi_psr != 0x2000 ||
        (record.post_rfi_psr & (PSR_I | PSR_IC | PSR_AC)) != PSR_IC ||
        record.post_rfi_iva != reinterpret_cast<uint64_t>(ia64_ivt) ||
        record.post_rfi_valid_marker != NAT_CONSUMPTION_POST_RFI_VALID)
        errors |= 1ull << 7;
    if (record.pre_fault_unat != record.post_rfi_unat ||
        record.pre_fault_gr_checksum != record.post_rfi_gr_checksum)
        errors |= 1ull << 8;
    return errors;
}

bool record_is_zero(const volatile NatConsumptionRecord& record)
{
    const volatile uint64_t* words = reinterpret_cast<const volatile uint64_t*>(&record);
    for (uint64_t i = 0; i != NAT_CONSUMPTION_RECORD_SIZE / sizeof(uint64_t); ++i) {
        if (words[i] != 0) return false;
    }
    return true;
}

void report_record(const volatile NatConsumptionRecord& record)
{
    ski_console::puts("IA64_NAT_CONSUMPTION_RECORD");
#define PRINT_FIELD(label, field) \
    ski_console::puts(" " label "="); ski_console::put_hex(record.field)
    PRINT_FIELD("valid", valid_marker);
    PRINT_FIELD("version", schema_version);
    PRINT_FIELD("size", record_size);
    PRINT_FIELD("sequence", sequence);
    PRINT_FIELD("vector", vector_offset);
    PRINT_FIELD("subtype", fault_subtype);
    PRINT_FIELD("site", fault_site);
    PRINT_FIELD("IIP", saved_iip);
    PRINT_FIELD("slot", saved_slot);
    PRINT_FIELD("IPSR", saved_ipsr);
    PRINT_FIELD("IIPA", saved_iipa);
    PRINT_FIELD("IFS", saved_ifs);
    PRINT_FIELD("IFS.valid", saved_ifs_valid);
    PRINT_FIELD("IFA", saved_ifa);
    PRINT_FIELD("IFA.valid", ifa_architecturally_valid);
    PRINT_FIELD("ISR", saved_isr);
    PRINT_FIELD("ISR.ed", isr_ed);
    PRINT_FIELD("ISR.ei", isr_ei);
    PRINT_FIELD("ISR.so", isr_so);
    PRINT_FIELD("ISR.ni", isr_ni);
    PRINT_FIELD("ISR.ir", isr_ir);
    PRINT_FIELD("ISR.rs", isr_rs);
    PRINT_FIELD("ISR.sp", isr_sp);
    PRINT_FIELD("ISR.na", isr_na);
    PRINT_FIELD("ISR.r", isr_read);
    PRINT_FIELD("ISR.w", isr_write);
    PRINT_FIELD("ISR.x", isr_execute);
    PRINT_FIELD("IIB0", saved_iib0);
    PRINT_FIELD("IIB1", saved_iib1);
    PRINT_FIELD("IIB0.available", saved_iib0_available);
    PRINT_FIELD("IIB1.available", saved_iib1_available);
    PRINT_FIELD("handler.PSR", handler_entry_psr);
    PRINT_FIELD("handler.RSC", handler_entry_rsc);
    PRINT_FIELD("handler.BSP", handler_entry_bsp);
    PRINT_FIELD("PR", saved_predicates);
    PRINT_FIELD("source.GR", source_register);
    PRINT_FIELD("destination.GR", destination_register);
    PRINT_FIELD("spec.site", speculative_load_site);
    PRINT_FIELD("pre.PSR", pre_fault_psr);
    PRINT_FIELD("spec.PSR", speculative_load_psr);
    PRINT_FIELD("post.spec.PSR", post_speculative_load_psr);
    PRINT_FIELD("pre.RSC", pre_fault_rsc);
    PRINT_FIELD("pre.BSP", pre_fault_bsp);
    PRINT_FIELD("pre.UNAT", pre_fault_unat);
    PRINT_FIELD("source.NAT.before", source_nat_before_fault);
    PRINT_FIELD("source.NAT.handler", source_nat_at_handler);
    PRINT_FIELD("source.NAT.after", source_nat_after_rfi);
    PRINT_FIELD("source.NAT.cleared", source_nat_after_clear);
    PRINT_FIELD("source.initial", source_initial_value);
    PRINT_FIELD("target", return_target);
    PRINT_FIELD("return.IPSR", return_ipsr);
    PRINT_FIELD("handler.status", handler_status);
    PRINT_FIELD("completion", completion_marker);
    PRINT_FIELD("count", continuation_count);
    PRINT_FIELD("return", return_marker);
    PRINT_FIELD("post.PSR", post_rfi_psr);
    PRINT_FIELD("post.RSC", post_rfi_rsc);
    PRINT_FIELD("post.BSP", post_rfi_bsp);
    PRINT_FIELD("post.UNAT", post_rfi_unat);
    PRINT_FIELD("pre.GR", pre_fault_gr_checksum);
    PRINT_FIELD("post.GR", post_rfi_gr_checksum);
    PRINT_FIELD("test", test_result);
    PRINT_FIELD("post.valid", post_rfi_valid_marker);
    PRINT_FIELD("post.IVA", post_rfi_iva);
#undef PRINT_FIELD
    ski_console::puts("\r\n");
}

} // namespace

bool run_nat_consumption_qualification()
{
    if (!record_is_zero(ia64_nat_consumption_record)) {
        ski_console::puts("IA64_NAT_CONSUMPTION_RESULT=FAIL reason=stale-record\r\n");
        return false;
    }

    ski_console::puts("IA64_NAT_CONSUMPTION_PRE_FAULT vector=0x5600 subtype=register-nat-consumption ");
    ski_console::puts("speculative=ld8.s PSR.ed=1 source=GR8 consumer=ld8 slot=0 expected_faults=1\r\n");
    const int test_result = ia64_nat_consumption_test();
    const uint64_t errors = validate_record(ia64_nat_consumption_record, test_result);
    report_record(ia64_nat_consumption_record);
    ski_console::puts("IA64_NAT_CONSUMPTION_VALIDATION errors=");
    ski_console::put_hex(errors);
    ski_console::puts(" test=");
    ski_console::put_hex(static_cast<uint64_t>(test_result));
    ski_console::puts("\r\n");
    ski_console::puts("IA64_NAT_CONSUMPTION_POST_RETURN target=");
    ski_console::put_hex(ia64_nat_consumption_record.return_target);
    ski_console::puts(" count=");
    ski_console::put_hex(ia64_nat_consumption_record.continuation_count);
    ski_console::puts(" PSR=");
    ski_console::put_hex(ia64_nat_consumption_record.post_rfi_psr);
    ski_console::puts(" source-NAT-cleared=");
    ski_console::put_hex(ia64_nat_consumption_record.source_nat_after_clear);
    ski_console::puts("\r\n");
    if (test_result != 0 || errors != 0) {
        ski_console::puts("IA64_NAT_CONSUMPTION_RESULT=FAIL\r\n");
        return false;
    }
    ski_console::puts("IA64_NAT_CONSUMPTION_RESULT=PASS\r\n");
    return true;
}

} // namespace ia64
} // namespace arch
} // namespace kernel
