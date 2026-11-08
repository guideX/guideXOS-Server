// Bounded reporting for IA-64 early-exec CPU-state checkpoints.

#include "include/arch/cpu_state_checkpoint.h"
#include "include/arch/ski_console.h"

extern "C" char ia64_ivt[];
extern "C" char rse_backing_store_base[];
extern "C" char rse_backing_store_top[];

namespace kernel {
namespace arch {
namespace ia64 {
namespace {

void report_field(const char* name, const volatile CpuStateCheckpoint& checkpoint,
                  uint64_t field, uint64_t value)
{
    ski_console::puts(" ");
    ski_console::puts(name);
    ski_console::puts("=");
    if (checkpoint.valid_fields & field) {
        ski_console::put_hex(value);
    } else {
        ski_console::puts("UNVERIFIED");
    }
}

void report_checkpoint(const char* name, const volatile CpuStateCheckpoint& checkpoint)
{
    ski_console::puts("IA64_CHECKPOINT_");
    ski_console::puts(name);
    ski_console::puts(" stage=");
    ski_console::put_hex(checkpoint.stage);
    ski_console::puts(" valid=");
    ski_console::put_hex(checkpoint.valid_fields);

    report_field("PSR", checkpoint, CHECKPOINT_HAS_PSR, checkpoint.psr);
    if (checkpoint.valid_fields & CHECKPOINT_HAS_PSR) {
        ski_console::puts(" PSR.i=");
        ski_console::putchar((checkpoint.psr & (1ull << 14)) ? '1' : '0');
        ski_console::puts(" PSR.ic=");
        ski_console::putchar((checkpoint.psr & (1ull << 13)) ? '1' : '0');
    }
    report_field("RSC", checkpoint, CHECKPOINT_HAS_RSC, checkpoint.rsc);
    if (checkpoint.valid_fields & CHECKPOINT_HAS_RSC) {
        ski_console::puts(" RSC.mode=");
        ski_console::putchar(static_cast<char>('0' + (checkpoint.rsc & 3ull)));
    }
    report_field("BSP", checkpoint, CHECKPOINT_HAS_BSP, checkpoint.bsp);
    report_field("BSPSTORE", checkpoint, CHECKPOINT_HAS_BSPSTORE, checkpoint.bspstore);
    report_field("RNAT", checkpoint, CHECKPOINT_HAS_RNAT, checkpoint.rnat);
    report_field("FPSR", checkpoint, CHECKPOINT_HAS_FPSR, checkpoint.fpsr);
    report_field("IVA", checkpoint, CHECKPOINT_HAS_IVA, checkpoint.iva);
    report_field("PFS", checkpoint, CHECKPOINT_HAS_PFS, checkpoint.pfs);
    report_field("UNAT", checkpoint, CHECKPOINT_HAS_UNAT, checkpoint.unat);

    ski_console::puts(" IVT_EXPECTED=");
    ski_console::put_hex(reinterpret_cast<uint64_t>(ia64_ivt));
    ski_console::puts(" RSE_RANGE=[");
    ski_console::put_hex(reinterpret_cast<uint64_t>(rse_backing_store_base));
    ski_console::puts(",");
    ski_console::put_hex(reinterpret_cast<uint64_t>(rse_backing_store_top));
    ski_console::puts(")\r\n");
}

} // namespace

void report_startup_cpu_state_checkpoints()
{
    report_checkpoint("A", ia64_checkpoint_a);
    report_checkpoint("B", ia64_checkpoint_b);
    report_checkpoint("C", ia64_checkpoint_c);
}

void report_preexit_cpu_state_checkpoint()
{
    report_checkpoint("D", ia64_checkpoint_d);
}

} // namespace ia64
} // namespace arch
} // namespace kernel
