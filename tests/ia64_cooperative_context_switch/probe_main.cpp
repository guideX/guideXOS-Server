#include "context.h"

#include <arch/ski_console.h>

namespace ia64_coop_probe {

extern "C" {
alignas(512) Context ia64_coop_context_a = {};
alignas(512) Context ia64_coop_context_b = {};
ProbeRecord ia64_coop_probe_record = {};
}

static_assert(offsetof(ProbeRecord, errors) == 200, "error counter offset changed");
static_assert(offsetof(ProbeRecord, nat_a_before) == 208, "A NaT record offset changed");
static_assert(offsetof(ProbeRecord, nat_b_before) == 224, "B NaT record offset changed");
static_assert(offsetof(ProbeRecord, psr_a_after_resume) == 240, "A PSR offset changed");
static_assert(offsetof(ProbeRecord, fp_a_low) == 256, "A FP record offset changed");
static_assert(offsetof(ProbeRecord, pfs_a_before) == 288, "A PFS offset changed");
static_assert(offsetof(ProbeRecord, fp_b_first_low) == 320, "B first FP offset changed");
static_assert(offsetof(ProbeRecord, a_final_count) == 336, "final count offset changed");
static_assert(offsetof(ProbeRecord, fp_a_first_resume) == 352,
              "A first-resume FP offset changed");
static_assert(offsetof(ProbeRecord, fp_a_final_resume) == 672,
              "A final-resume FP offset changed");
static_assert(offsetof(ProbeRecord, fp_b_first_entry) == 992,
              "B first-entry FP offset changed");
static_assert(offsetof(ProbeRecord, fp_b_resume) == 1312,
              "B resume FP offset changed");
static_assert(sizeof(ProbeRecord) == 1632, "probe record size changed");

} // namespace ia64_coop_probe

extern "C" uint64_t ia64_coop_stack_a_guard_low;
extern "C" uint64_t ia64_coop_stack_a_bottom;
extern "C" uint64_t ia64_coop_stack_a_top;
extern "C" uint64_t ia64_coop_stack_a_guard_high;
extern "C" uint64_t ia64_coop_stack_b_guard_low;
extern "C" uint64_t ia64_coop_stack_b_bottom;
extern "C" uint64_t ia64_coop_stack_b_top;
extern "C" uint64_t ia64_coop_stack_b_guard_high;
extern "C" uint64_t ia64_coop_rbs_a_guard_low;
extern "C" uint64_t ia64_coop_rbs_a_base;
extern "C" uint64_t ia64_coop_rbs_a_limit;
extern "C" uint64_t ia64_coop_rbs_a_guard_high;
extern "C" uint64_t ia64_coop_rbs_b_guard_low;
extern "C" uint64_t ia64_coop_rbs_b_base;
extern "C" uint64_t ia64_coop_rbs_b_limit;
extern "C" uint64_t ia64_coop_rbs_b_guard_high;
extern "C" uint64_t __gp;

namespace {

using namespace ia64_coop_probe;
using ia64_coop_probe::Context;
using ia64_coop_probe::ProbeRecord;
using kernel::arch::ia64::ski_console::put_hex;
using kernel::arch::ia64::ski_console::puts;
using kernel::arch::ia64::ski_console::stop;

static const uint64_t kExpectedEvents[8] = {1, 2, 3, 4, 5, 6, 7, 8};
static const uint64_t kExpectedContexts[8] = {0xA, 0xB, 0xB, 0xA, 0xA, 0xB, 0xB, 0xA};
static const uint64_t kEventNames[8] = {0, 1, 2, 3, 4, 5, 6, 7};
static const char* const kEventLabels[8] = {
    "A_TO_B", "B_FIRST_ENTRY", "B_TO_A_FIRST", "A_RESUMED_FIRST",
    "A_TO_B_SECOND", "B_RESUMED", "B_TO_A_SECOND", "A_RESUMED_FINAL"
};

uint64_t address(const void* pointer)
{
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pointer));
}

bool overlaps(uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1)
{
    return a0 < b1 && b0 < a1;
}

bool in_range(uint64_t value, uint64_t low, uint64_t high)
{
    return low <= value && value <= high;
}

bool context_geometry_ok(const Context& context)
{
    return context.stack_base < context.stack_limit &&
           in_range(context.sp, context.stack_base, context.stack_limit) &&
           context.observed_sp == context.sp && (context.sp & 15) == 0 &&
           context.code_base < context.code_limit &&
           context.code_base <= context.rp && context.rp < context.code_limit &&
           (context.rp & 15) == 0 &&
           context.rbs_base < context.rbs_limit &&
           context.rbs_base <= context.bsp && context.bsp <= context.rbs_limit &&
           context.rbs_base <= context.bspstore && context.bspstore <= context.rbs_limit &&
           (context.rbs_base & 511) == 0 && (context.rbs_limit & 511) == 0 &&
           context.bsp == context.bspstore && context.observed_bsp == context.bsp &&
           context.observed_bspstore == context.bspstore && context.rsc == 3 &&
           (context.observed_psr & kPsrInterruptEnable) == 0 &&
           context.stack_canary_low == kStackCanaryLow &&
           context.stack_canary_high == kStackCanaryHigh &&
           context.rbs_canary_low == kRbsCanaryLow &&
           context.rbs_canary_high == kRbsCanaryHigh;
}

uint64_t rnat_mask_for(uint64_t bspstore)
{
    const uint64_t index = (bspstore >> 3) & 0x3f;
    if (index >= 63) return 0;
    return (static_cast<uint64_t>(1) << (index + 1)) - 1;
}

bool rnat_ok(const Context& context)
{
    const uint64_t expected_mask = rnat_mask_for(context.bspstore);
    return expected_mask != 0 && context.rnat_valid_mask == expected_mask &&
           context.rnat_defined_bits == (context.rnat & expected_mask) &&
           (context.rnat_defined_bits & ~context.rnat_valid_mask) == 0;
}

bool fp_images_match(const FpImage* images, uint64_t low_base, uint64_t high)
{
    for (uint64_t index = 0; index < 20; ++index) {
        if (images[index].low != low_base + index * kFpImageStride ||
            images[index].high != high) return false;
    }
    return true;
}

uint64_t validate(const ProbeRecord& record, const Context& a, const Context& b)
{
    uint64_t errors = record.errors;
    if (record.event_count != 8) ++errors;
    for (uint64_t i = 0; i < 8; ++i) {
        const ia64_coop_probe::Event& event = record.events[i];
        if (event.sequence != kExpectedEvents[i] ||
            event.context_id != kExpectedContexts[i] ||
            event.event_code != kEventNames[i] + 1) {
            ++errors;
        }
    }

    if (!context_geometry_ok(a) || !context_geometry_ok(b) ||
        !rnat_ok(a) || !rnat_ok(b)) ++errors;
    if (a.switch_count != 2 || a.first_entries != 0 || a.resume_entries != 2 ||
        b.switch_count != 2 || b.first_entries != 1 || b.resume_entries != 1 ||
        a.first_entry != 0 || b.first_entry != 0) ++errors;
    if (a.context_id != kContextA || b.context_id != kContextB ||
        a.profile_magic != kProfileMagic || b.profile_magic != kProfileMagic) ++errors;
    if (a.gp != address(&__gp) || b.gp != 0) ++errors;

    const uint64_t stack_a0 = address(&ia64_coop_stack_a_bottom);
    const uint64_t stack_a1 = address(&ia64_coop_stack_a_top);
    const uint64_t stack_b0 = address(&ia64_coop_stack_b_bottom);
    const uint64_t stack_b1 = address(&ia64_coop_stack_b_top);
    const uint64_t rbs_a0 = address(&ia64_coop_rbs_a_base);
    const uint64_t rbs_a1 = address(&ia64_coop_rbs_a_limit);
    const uint64_t rbs_b0 = address(&ia64_coop_rbs_b_base);
    const uint64_t rbs_b1 = address(&ia64_coop_rbs_b_limit);
    if (a.stack_base != stack_a0 || a.stack_limit != stack_a1 ||
        b.stack_base != stack_b0 || b.stack_limit != stack_b1 ||
        a.rbs_base != rbs_a0 || a.rbs_limit != rbs_a1 ||
        b.rbs_base != rbs_b0 || b.rbs_limit != rbs_b1 ||
        stack_a1 - stack_a0 != 65536 || stack_b1 - stack_b0 != 65536 ||
        rbs_a1 - rbs_a0 != 65536 || rbs_b1 - rbs_b0 != 65536 ||
        overlaps(stack_a0, stack_a1, stack_b0, stack_b1) ||
        overlaps(rbs_a0, rbs_a1, rbs_b0, rbs_b1) ||
        overlaps(stack_a0, stack_a1, rbs_a0, rbs_a1) ||
        overlaps(stack_a0, stack_a1, rbs_b0, rbs_b1) ||
        overlaps(stack_b0, stack_b1, rbs_a0, rbs_a1) ||
        overlaps(stack_b0, stack_b1, rbs_b0, rbs_b1)) ++errors;
    if (a.sp != stack_a1 - 4096 || b.sp != stack_b1 - 4096 ||
        *reinterpret_cast<const uint64_t*>(a.sp) != kStackUseALow ||
        *reinterpret_cast<const uint64_t*>(a.sp + 8) != kStackUseAHigh ||
        *reinterpret_cast<const uint64_t*>(b.sp) != kStackUseBLow ||
        *reinterpret_cast<const uint64_t*>(b.sp + 8) != kStackUseBHigh) ++errors;

    const uint64_t context_a_address = address(&a);
    const uint64_t context_b_address = address(&b);
    if ((context_a_address & 511) != 0 || (context_b_address & 511) != 0 ||
        overlaps(context_a_address, context_a_address + sizeof(Context),
                 context_b_address, context_b_address + sizeof(Context))) ++errors;

    if (record.nat_a_before != 1 || record.nat_a_after != 1 ||
        record.nat_b_before != 0 || record.nat_b_after != 0) ++errors;
    if (record.pfs_a_before != record.pfs_a_after ||
        record.pfs_b_before != record.pfs_b_after) ++errors;
    if (record.a_final_count != 1) ++errors;
    if (record.fp_a_low != kFpALow || record.fp_a_high != kFpAHigh ||
        record.fp_b_low != kFpBLow || record.fp_b_high != kFpBHigh ||
        record.fp_b_first_low != kFpBLow || record.fp_b_first_high != kFpBHigh) ++errors;
    if (!fp_images_match(record.fp_a_first_resume, kFpALow, kFpAHigh) ||
        !fp_images_match(record.fp_a_final_resume, kFpALow, kFpAHigh) ||
        !fp_images_match(record.fp_b_first_entry, kFpBLow, kFpBHigh) ||
        !fp_images_match(record.fp_b_resume, kFpBLow, kFpBHigh)) ++errors;
    if ((record.psr_a_after_resume & kPsrInterruptEnable) != 0 ||
        (record.psr_b_after_resume & kPsrInterruptEnable) != 0) ++errors;

    const uint64_t canaries[] = {
        ia64_coop_stack_a_guard_low, ia64_coop_stack_a_guard_high,
        ia64_coop_stack_b_guard_low, ia64_coop_stack_b_guard_high
    };
    const uint64_t expected_stack[] = {kStackCanaryLow, kStackCanaryHigh,
                                       kStackCanaryLow, kStackCanaryHigh};
    const uint64_t rbs_canaries[] = {
        ia64_coop_rbs_a_guard_low, ia64_coop_rbs_a_guard_high,
        ia64_coop_rbs_b_guard_low, ia64_coop_rbs_b_guard_high
    };
    const uint64_t expected_rbs[] = {kRbsCanaryLow, kRbsCanaryHigh,
                                     kRbsCanaryLow, kRbsCanaryHigh};
    for (uint64_t i = 0; i < 4; ++i) {
        if (canaries[i] != expected_stack[i] || rbs_canaries[i] != expected_rbs[i]) ++errors;
    }
    return errors;
}

void report_context(const char* name, const Context& context)
{
    puts("IA64_COOP_CONTEXT_STATE id=");
    puts(name);
    puts(" stack_base=");
    put_hex(context.stack_base);
    puts(" stack_limit=");
    put_hex(context.stack_limit);
    puts(" sp=");
    put_hex(context.sp);
    puts(" rp=");
    put_hex(context.rp);
    puts(" code_base=");
    put_hex(context.code_base);
    puts(" code_limit=");
    put_hex(context.code_limit);
    puts(" rbs_base=");
    put_hex(context.rbs_base);
    puts(" rbs_limit=");
    put_hex(context.rbs_limit);
    puts(" gp=");
    put_hex(context.gp);
    puts(" switches=");
    put_hex(context.switch_count);
    puts(" first_entries=");
    put_hex(context.first_entries);
    puts(" resumes=");
    put_hex(context.resume_entries);
    puts(" bsp=");
    put_hex(context.bsp);
    puts(" bspstore=");
    put_hex(context.bspstore);
    puts(" rnat_defined=");
    put_hex(context.rnat_defined_bits);
    puts(" rnat_valid_mask=");
    put_hex(context.rnat_valid_mask);
    puts(" observed_psr=");
    put_hex(context.observed_psr);
    puts("\r\n");
}

} // namespace

extern "C" void ia64_coop_probe_main()
{
    using namespace ia64_coop_probe;
    puts("IA64_COOP_CONTEXT_PROBE_START mode=forced-system cpu_count=1\r\n");
    for (uint64_t i = 0; i < 8; ++i) {
        puts("IA64_COOP_EVENT sequence=");
        put_hex(ia64_coop_probe_record.events[i].sequence);
        puts(" context=");
        put_hex(ia64_coop_probe_record.events[i].context_id);
        puts(" event=");
        puts(kEventLabels[i]);
        puts("\r\n");
    }
    report_context("A", ia64_coop_context_a);
    report_context("B", ia64_coop_context_b);
    const uint64_t errors = validate(ia64_coop_probe_record,
                                     ia64_coop_context_a, ia64_coop_context_b);
    const bool fp_images_ok =
        fp_images_match(ia64_coop_probe_record.fp_a_first_resume, kFpALow, kFpAHigh) &&
        fp_images_match(ia64_coop_probe_record.fp_a_final_resume, kFpALow, kFpAHigh) &&
        fp_images_match(ia64_coop_probe_record.fp_b_first_entry, kFpBLow, kFpBHigh) &&
        fp_images_match(ia64_coop_probe_record.fp_b_resume, kFpBLow, kFpBHigh);
    puts("IA64_COOP_FP_VALIDATION images=0x50 registers_per_context=0x14 valid=");
    put_hex(fp_images_ok ? 1 : 0);
    puts("\r\n");
    puts("IA64_COOP_CONTEXT_VALIDATION errors=");
    put_hex(errors);
    puts(" nat_a_before=");
    put_hex(ia64_coop_probe_record.nat_a_before);
    puts(" nat_a_after=");
    put_hex(ia64_coop_probe_record.nat_a_after);
    puts(" nat_b_before=");
    put_hex(ia64_coop_probe_record.nat_b_before);
    puts(" nat_b_after=");
    put_hex(ia64_coop_probe_record.nat_b_after);
    puts(" pfs_a_before=");
    put_hex(ia64_coop_probe_record.pfs_a_before);
    puts(" pfs_a_after=");
    put_hex(ia64_coop_probe_record.pfs_a_after);
    puts(" pfs_b_before=");
    put_hex(ia64_coop_probe_record.pfs_b_before);
    puts(" pfs_b_after=");
    put_hex(ia64_coop_probe_record.pfs_b_after);
    puts(" psr_a=");
    put_hex(ia64_coop_probe_record.psr_a_after_resume);
    puts(" psr_b=");
    put_hex(ia64_coop_probe_record.psr_b_after_resume);
    puts(" fp_a_low=");
    put_hex(ia64_coop_probe_record.fp_a_low);
    puts(" fp_a_high=");
    put_hex(ia64_coop_probe_record.fp_a_high);
    puts(" fp_b_first_low=");
    put_hex(ia64_coop_probe_record.fp_b_first_low);
    puts(" fp_b_first_high=");
    put_hex(ia64_coop_probe_record.fp_b_first_high);
    puts(" fp_b_resume_low=");
    put_hex(ia64_coop_probe_record.fp_b_low);
    puts(" fp_b_resume_high=");
    put_hex(ia64_coop_probe_record.fp_b_high);
    puts("\r\n");
    if (errors == 0) {
        puts("IA64_COOP_CONTEXT_PROBE_RESULT=PASS\r\n");
    } else {
        puts("IA64_COOP_CONTEXT_PROBE_RESULT=FAIL\r\n");
    }
    stop();
    for (;;) asm volatile("hint @pause");
}
