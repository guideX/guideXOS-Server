/* Independent standalone Unaligned Reference qualification entry. */
#include <arch/unaligned_reference.h>
#include <arch/ski_console.h>

extern "C" void unaligned_reference_probe_main()
{
    using namespace kernel::arch::ia64;
    ski_console::init();
    ski_console::puts("IA64_UNALIGNED_REFERENCE_PROBE_START vector=0x5a00 access=ld8\r\n");
    const bool passed = run_unaligned_reference_qualification();
    if (passed) {
        ski_console::puts("IA64_UNALIGNED_REFERENCE_PROBE_RESULT=PASS\r\n");
    } else {
        ski_console::puts("IA64_UNALIGNED_REFERENCE_PROBE_RESULT=FAIL\r\n");
    }
    ski_console::stop();
    for (;;) asm volatile("hint @pause");
}
