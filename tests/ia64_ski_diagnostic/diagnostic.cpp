/* Uses the real IA-64 register readers and Ski console implementation. */
#include <arch/ia64.h>
#include <arch/ski_console.h>

using kernel::arch::ia64::read_ar;
using kernel::arch::ia64::read_bsp;
using kernel::arch::ia64::read_iva;
using kernel::arch::ia64::read_psr;
using kernel::arch::ia64::write_iva;
using kernel::arch::ia64::ski_console::put_hex;
using kernel::arch::ia64::ski_console::putchar;
using kernel::arch::ia64::ski_console::puts;

extern "C" {
volatile uint64_t diag_boot_snapshot_iva;
volatile uint64_t diag_boot_snapshot_bsp;
volatile uint64_t diag_boot_snapshot_bspstore;
volatile uint64_t diag_boot_expected_bspstore;
volatile uint64_t diag_boot_snapshot_rsc;
volatile uint64_t diag_boot_snapshot_psr;
extern char diag_ivt;
extern char diag_rse_backing_store_base;
}

static const uint64_t kDiagnosticFpsr = 0x0009804c0270033fULL;

namespace {

void report(const char* label, uint64_t value)
{
    puts(label);
    put_hex(value);
    puts("\r\n");
}

__attribute__((noinline)) uint64_t keep_value_across_console(uint64_t value)
{
    puts("LIVE-HELD:");
    putchar('!');
    puts("|");
    return value;
}

uint64_t emit_format_regression_values()
{
    static const uint64_t values[] = {
        0,
        0x77,
        0x100000,
        0x4c6fe00,
        0x4c7fe00,
        0x123456789abcdef0,
        0xffffffffffffffff,
    };

    puts("FORMAT-BEGIN\r\n");
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        puts("HEX[");
        putchar(static_cast<char>('0' + i));
        puts("]=");
        put_hex(values[i]);
        puts("\r\n");
    }

    puts("REPEAT=");
    put_hex(values[1]);
    putchar('|');
    put_hex(values[5]);
    putchar('|');
    put_hex(values[1]);
    puts("\r\n");

    const uint64_t retained = keep_value_across_console(values[5]);
    puts("LIVE-RETURN=");
    put_hex(retained);
    puts("\r\nFORMAT-END\r\n");
    return retained;
}

} // namespace

extern "C" void diagnostic_main()
{
    using namespace kernel::arch::ia64;
    using namespace kernel::arch::ia64::ski_console;

    ski_console::init();
    const int input_byte = ski_console::getchar();
    const uint64_t live_value = emit_format_regression_values();

    /* Snapshot the current C++ readers before these reporting calls execute. */
    const uint64_t current_bsp = read_bsp();
    const uint64_t current_rsc = read_ar<16>();
    const uint64_t current_psr = read_psr();
    write_ar<40>(kDiagnosticFpsr);
    const uint64_t current_fpsr = read_ar<40>();

    const uint64_t second_iva = 0x200000;
    write_iva(second_iva);
    asm volatile("srlz.i ;;" : : : "memory");
    const uint64_t iva_through_helper = read_iva();
    const uint64_t iva_through_template = read_cr<2>();
    write_iva(reinterpret_cast<uint64_t>(&diag_ivt));
    asm volatile("srlz.i ;;" : : : "memory");

    puts("REGISTER-BEGIN\r\n");
    report("IVA.STARTUP=", diag_boot_snapshot_iva);
    report("IVA.EXPECTED=", reinterpret_cast<uint64_t>(&diag_ivt));
    report("IVA.READ_HELPER=", iva_through_helper);
    report("IVA.READ_TEMPLATE=", iva_through_template);
    report("BSP.STARTUP=", diag_boot_snapshot_bsp);
    report("BSPSTORE.STARTUP=", diag_boot_snapshot_bspstore);
    report("BSPSTORE.EXPECTED=", diag_boot_expected_bspstore);
    report("RSC.STARTUP=", diag_boot_snapshot_rsc);
    puts("RNAT.STARTUP=UNVERIFIED\r\n");
    report("PSR.STARTUP=", diag_boot_snapshot_psr);
    report("BSP.CURRENT=", current_bsp);
    report("RSC.CURRENT=", current_rsc);
    report("PSR.CURRENT=", current_psr);
    report("FPSR.EXPECTED=", kDiagnosticFpsr);
    report("FPSR.READBACK=", current_fpsr);
    puts("SSC_GETCHAR=");
    put_hex(static_cast<uint8_t>(input_byte));
    puts("\r\n");
    puts(input_byte == 'Q' ? "SSC_GETCHAR_API=PASS\r\n" : "SSC_GETCHAR_API=FAIL\r\n");

    const uint64_t psr_interrupt_masks = (1ULL << 13) | (1ULL << 14);
    const bool pass =
        input_byte == 'Q' &&
        live_value == 0x123456789abcdef0 &&
        diag_boot_snapshot_iva == reinterpret_cast<uint64_t>(&diag_ivt) &&
        iva_through_helper == second_iva && iva_through_template == second_iva &&
        diag_boot_snapshot_bspstore == diag_boot_expected_bspstore &&
        diag_boot_snapshot_rsc == 3 && current_rsc == 3 &&
        current_fpsr == kDiagnosticFpsr &&
        (diag_boot_snapshot_psr & psr_interrupt_masks) == 0 &&
        (current_psr & psr_interrupt_masks) == 0;
    puts(pass ? "REGISTER_PROBE=PASS\r\n" : "REGISTER_PROBE=FAIL\r\n");
    puts(current_fpsr == kDiagnosticFpsr ? "FPSR_READBACK=PASS\r\n" : "FPSR_READBACK=FAIL\r\n");
    puts("REGISTER-END\r\n");
    stop();
    for (;;) {
        asm volatile("hint @pause");
    }
}
