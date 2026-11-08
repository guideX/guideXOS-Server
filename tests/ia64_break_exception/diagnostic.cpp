/* Standalone Ski diagnostic entry: report only after both rfi returns. */
#include <arch/break_exception.h>
#include <arch/ski_console.h>

using namespace kernel::arch::ia64;

namespace {

void report_record(const char* name, const volatile BreakExceptionRecord& record)
{
    ski_console::puts("IA64_BREAK_PROBE_RECORD ");
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

extern "C" void break_probe_main()
{
    ski_console::init();
    ski_console::puts("IA64_BREAK_PROBE_START vector=0x2C00 immediates=0x6A17,0x6A18\r\n");
    if (ia64_break_exception_record_slot1.valid_marker != 0 ||
        ia64_break_exception_record_slot2.valid_marker != 0) {
        ski_console::puts("IA64_BREAK_PROBE_RESULT=FAIL reason=stale-record\r\n");
        ski_console::stop();
        for (;;) asm volatile("hint @pause");
    }

    const int test_result = ia64_break_exception_test();
    const uint64_t errors = ia64_break_exception_validate_records();
    report_record("slot1", ia64_break_exception_record_slot1);
    report_record("slot2", ia64_break_exception_record_slot2);
    ski_console::puts("IA64_BREAK_PROBE_VALIDATION errors=");
    ski_console::put_hex(errors);
    ski_console::puts(" test=");
    ski_console::put_hex(static_cast<uint64_t>(test_result));
    ski_console::puts("\r\n");
    if (test_result == 0 && errors == 0) {
        ski_console::puts("IA64_BREAK_EXCEPTION_RESULT=PASS\r\n");
        ski_console::puts("IA64_BREAK_PROBE_RESULT=PASS\r\n");
    } else {
        ski_console::puts("IA64_BREAK_EXCEPTION_RESULT=FAIL\r\n");
        ski_console::puts("IA64_BREAK_PROBE_RESULT=FAIL\r\n");
    }
    ski_console::stop();
    for (;;) asm volatile("hint @pause");
}
