#include "include/arch/stacked_nat_rse.h"
#include "include/arch/ski_console.h"

namespace kernel {
namespace arch {
namespace ia64 {
namespace {

uint64_t rnat_valid_mask(uint64_t index)
{
    if (index >= 63) return 0x7fffffffffffffffull;
    return (1ull << (index + 1)) - 1;
}

uint64_t selected_rnat_value(const volatile StackedNatRseRecord& record)
{
    if (record.rnat_source == 1) {
        return (record.child_rnat >> record.nat_slot_index) & 1ull;
    }
    if (record.rnat_source == 2) {
        return (record.rnat_collection_value >> record.nat_slot_index) & 1ull;
    }
    return 0;
}

} // namespace

bool stacked_nat_rse_record_is_zero()
{
    const volatile uint64_t* words =
        reinterpret_cast<const volatile uint64_t*>(&ia64_stacked_nat_rse_record);
    for (uint64_t i = 0; i != STACKED_NAT_RSE_RECORD_SIZE / sizeof(uint64_t); ++i) {
        if (words[i] != 0) return false;
    }
    return true;
}

uint64_t validate_stacked_nat_rse_record(const volatile StackedNatRseRecord& record)
{
    uint64_t errors = 0;
    const uint64_t store_base = reinterpret_cast<uint64_t>(rse_backing_store_base);
    const uint64_t store_top = reinterpret_cast<uint64_t>(rse_backing_store_top);

    if (record.magic != STACKED_NAT_RSE_MAGIC || record.version != 1 ||
        record.size != STACKED_NAT_RSE_RECORD_SIZE || record.sequence != 1 ||
        record.result != 0 || record.stage != 4 ||
        record.return_marker != STACKED_NAT_RSE_RETURN_MARKER)
        errors |= 1ull << 0;

    if (record.static_nat_before != 1 || record.stacked_nat_before != 1 ||
        record.stacked_nat_after != 1 || record.static_nat_after != 1)
        errors |= 1ull << 1;

    if (record.parent_sof != STACKED_NAT_RSE_PARENT_SOF ||
        record.child_sof != STACKED_NAT_RSE_CHILD_SOF)
        errors |= 1ull << 2;

    if ((record.original_rsc & 3ull) != 3 || (record.lazy_rsc & 3ull) != 0 ||
        (record.child_rsc & 3ull) != 0 || record.restored_rsc != record.original_rsc)
        errors |= 1ull << 3;

    const uint64_t parent_index = (record.parent_bsp >> 3) & 0x3full;
    const uint64_t expected_nat_slot = record.parent_bsp +
        ((parent_index == 62) ? 16ull : 8ull);
    const uint64_t expected_collection = (expected_nat_slot & ~0x1ffull) | 0x1f8ull;
    if ((record.parent_bsp & 7ull) != 0 || parent_index > 62 ||
        record.nat_slot_address != expected_nat_slot ||
        record.nat_slot_index != ((expected_nat_slot >> 3) & 0x3full) ||
        record.nat_collection_address != expected_collection ||
        record.parent_bsp < store_base || record.parent_bsp + 1024 > store_top ||
        record.child_bsp <= record.parent_bsp || record.child_bsp >= store_top ||
        record.parent_bspstore != record.parent_bsp ||
        record.child_bspstore <= record.parent_bspstore ||
        record.child_bspstore < record.parent_bsp + 24 ||
        record.child_bspstore > store_top ||
        record.return_bsp != record.parent_bsp ||
        record.return_bspstore != record.parent_bspstore)
        errors |= 1ull << 4;

    if (record.ordinary_before != STACKED_NAT_RSE_SENTINEL ||
        record.spilled_sentinel != STACKED_NAT_RSE_SENTINEL ||
        record.ordinary_after != STACKED_NAT_RSE_SENTINEL)
        errors |= 1ull << 5;

    if (record.rnat_source == 1) {
        const uint64_t store_group = record.child_bspstore >> 9;
        const uint64_t store_index = (record.child_bspstore >> 3) & 0x3full;
        const uint64_t mask = rnat_valid_mask(store_index);
        if ((record.nat_slot_address >> 9) != store_group ||
            record.nat_slot_index > store_index ||
            ((record.child_rnat & mask) & (1ull << record.nat_slot_index)) == 0 ||
            selected_rnat_value(record) != 1)
            errors |= 1ull << 6;
    } else if (record.rnat_source == 2) {
        if (record.nat_collection_address + 8 > record.child_bspstore ||
            (record.nat_collection_address >> 9) != (record.nat_slot_address >> 9) ||
            selected_rnat_value(record) != 1)
            errors |= 1ull << 6;
    } else {
        errors |= 1ull << 6;
    }
    return errors;
}

uint64_t stacked_nat_rse_negative_contract_tests(
    const volatile StackedNatRseRecord& record)
{
    uint64_t failures = 0;
    StackedNatRseRecord changed;
    const volatile uint64_t* source =
        reinterpret_cast<const volatile uint64_t*>(&record);
    uint64_t* destination = reinterpret_cast<uint64_t*>(&changed);

    for (uint64_t i = 0; i != STACKED_NAT_RSE_RECORD_SIZE / sizeof(uint64_t); ++i)
        destination[i] = source[i];
    changed.stacked_nat_after = 0;
    if ((validate_stacked_nat_rse_record(changed) & (1ull << 1)) == 0)
        failures |= 1ull << 0;

    for (uint64_t i = 0; i != STACKED_NAT_RSE_RECORD_SIZE / sizeof(uint64_t); ++i)
        destination[i] = source[i];
    changed.spilled_sentinel ^= 1ull;
    if ((validate_stacked_nat_rse_record(changed) & (1ull << 5)) == 0)
        failures |= 1ull << 1;

    for (uint64_t i = 0; i != STACKED_NAT_RSE_RECORD_SIZE / sizeof(uint64_t); ++i)
        destination[i] = source[i];
    changed.magic ^= 1ull;
    if ((validate_stacked_nat_rse_record(changed) & (1ull << 0)) == 0)
        failures |= 1ull << 2;

    return failures;
}

void report_stacked_nat_rse_record(const volatile StackedNatRseRecord& record)
{
    ski_console::puts("IA64_STACKED_NAT_RSE_RECORD");
#define PRINT_FIELD(label, field) \
    ski_console::puts(" " label "="); ski_console::put_hex(record.field)
    PRINT_FIELD("magic", magic);
    PRINT_FIELD("version", version);
    PRINT_FIELD("size", size);
    PRINT_FIELD("sequence", sequence);
    PRINT_FIELD("result", result);
    PRINT_FIELD("stage", stage);
    PRINT_FIELD("static.NAT.before", static_nat_before);
    PRINT_FIELD("stacked.NAT.before", stacked_nat_before);
    PRINT_FIELD("stacked.NAT.after", stacked_nat_after);
    PRINT_FIELD("static.NAT.after", static_nat_after);
    PRINT_FIELD("ordinary.before", ordinary_before);
    PRINT_FIELD("ordinary.after", ordinary_after);
    PRINT_FIELD("frame.parent", parent_sof);
    PRINT_FIELD("frame.child", child_sof);
    PRINT_FIELD("RSC.original", original_rsc);
    PRINT_FIELD("RSC.lazy", lazy_rsc);
    PRINT_FIELD("RSC.child", child_rsc);
    PRINT_FIELD("RSC.restored", restored_rsc);
    PRINT_FIELD("BSP.parent", parent_bsp);
    PRINT_FIELD("BSPSTORE.before", parent_bspstore);
    PRINT_FIELD("BSP.child", child_bsp);
    PRINT_FIELD("BSPSTORE.child", child_bspstore);
    PRINT_FIELD("spill.sentinel", spilled_sentinel);
    PRINT_FIELD("BSP.return", return_bsp);
    PRINT_FIELD("BSPSTORE.return", return_bspstore);
    PRINT_FIELD("NaT.slot.address", nat_slot_address);
    PRINT_FIELD("NaT.collection.address", nat_collection_address);
    PRINT_FIELD("NaT.slot.index", nat_slot_index);
    PRINT_FIELD("RNAT.source", rnat_source);
    PRINT_FIELD("RNAT.observed", rnat_observed);
#undef PRINT_FIELD
    ski_console::puts("\r\n");

    ski_console::puts("IA64_STACKED_NAT_RSE_RNAT valid_mask=");
    if (record.rnat_source == 1) {
        ski_console::put_hex(rnat_valid_mask((record.child_bspstore >> 3) & 0x3full));
        ski_console::puts(" value=");
        ski_console::put_hex(record.child_rnat);
        ski_console::puts(" collection_memory=not-required");
    } else if (record.rnat_source == 2) {
        ski_console::put_hex(0x7fffffffffffffffull);
        ski_console::puts(" collection=");
        ski_console::put_hex(record.rnat_collection_value);
        ski_console::puts(" collection_memory=complete");
    } else {
        ski_console::puts("0x0 unavailable");
    }
    ski_console::puts("\r\n");
}

bool run_stacked_nat_rse_qualification()
{
    if (stacked_nat_rse_record_is_zero()) {
        ski_console::puts("IA64_STACKED_NAT_RSE_KERNEL_RESULT=FAIL reason=missing-record\r\n");
        return false;
    }
    const uint64_t errors = validate_stacked_nat_rse_record(ia64_stacked_nat_rse_record);
    const uint64_t negative_failures =
        stacked_nat_rse_negative_contract_tests(ia64_stacked_nat_rse_record);
    report_stacked_nat_rse_record(ia64_stacked_nat_rse_record);
    ski_console::puts("IA64_STACKED_NAT_RSE_VALIDATION errors=");
    ski_console::put_hex(errors);
    ski_console::puts(" negative_failures=");
    ski_console::put_hex(negative_failures);
    ski_console::puts("\r\n");
    if (errors != 0 || negative_failures != 0) {
        ski_console::puts("IA64_STACKED_NAT_RSE_KERNEL_RESULT=FAIL\r\n");
        return false;
    }
    ski_console::puts("IA64_STACKED_NAT_RSE_KERNEL_RESULT=PASS\r\n");
    return true;
}

} // namespace ia64
} // namespace arch
} // namespace kernel
