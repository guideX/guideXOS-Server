/* Standalone probe harness; the NaT stimulus and handler are hand-written assembly. */
#include <arch/nat_consumption.h>
#include <arch/ski_console.h>

extern "C" void nat_consumption_probe_main()
{
    using namespace kernel::arch::ia64;
    ski_console::init();
    ski_console::puts("IA64_NAT_CONSUMPTION_PROBE_START vector=0x5600\r\n");
    const bool passed = run_nat_consumption_qualification();
    if (passed) {
        ski_console::puts("IA64_NAT_CONSUMPTION_PROBE_RESULT=PASS\r\n");
    } else {
        ski_console::puts("IA64_NAT_CONSUMPTION_PROBE_RESULT=FAIL\r\n");
    }
    ski_console::stop();
    for (;;) asm volatile("hint @pause");
}
