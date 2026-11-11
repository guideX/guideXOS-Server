/* Independent standalone General Exception test entry. */
#include <arch/general_exception.h>
#include <arch/ski_console.h>

extern "C" void general_exception_probe_main()
{
    using namespace kernel::arch::ia64;
    ski_console::init();
    ski_console::puts("IA64_GENERAL_EXCEPTION_PROBE_START vector=0x5400 subtype=illegal-operation\r\n");
    const bool passed = run_general_exception_qualification();
    if (passed) {
        ski_console::puts("IA64_GENERAL_EXCEPTION_PROBE_RESULT=PASS\r\n");
    } else {
        ski_console::puts("IA64_GENERAL_EXCEPTION_PROBE_RESULT=FAIL\r\n");
    }
    ski_console::stop();
    for (;;) asm volatile("hint @pause");
}
