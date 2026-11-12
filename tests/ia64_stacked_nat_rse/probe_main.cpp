#include <arch/stacked_nat_rse.h>
#include <arch/ski_console.h>

extern "C" void stacked_nat_rse_probe_main()
{
    using namespace kernel::arch::ia64;
    ski_console::puts("IA64_STACKED_NAT_RSE_PROBE_START mode=forced-lazy\r\n");
    const uint64_t errors = validate_stacked_nat_rse_record(ia64_stacked_nat_rse_record);
    const uint64_t negative_failures =
        stacked_nat_rse_negative_contract_tests(ia64_stacked_nat_rse_record);
    report_stacked_nat_rse_record(ia64_stacked_nat_rse_record);
    ski_console::puts("IA64_STACKED_NAT_RSE_VALIDATION errors=");
    ski_console::put_hex(errors);
    ski_console::puts(" negative_failures=");
    ski_console::put_hex(negative_failures);
    ski_console::puts("\r\n");

    if (errors == 0 && negative_failures == 0) {
        ski_console::puts("IA64_STACKED_NAT_RSE_PROBE_RESULT=PASS\r\n");
    } else {
        ski_console::puts("IA64_STACKED_NAT_RSE_PROBE_RESULT=FAIL\r\n");
    }
    ski_console::stop();
    for (;;) asm volatile("hint @pause");
}
