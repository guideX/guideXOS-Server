#include "include/kernel/common_kernel_entry.h"

namespace kernel {
namespace common {

bool kernel_entry_init(const KernelEntryConfig& config)
{
    if (!config.allocate_pages || config.stack_pages < 2 ||
        config.cooperative_switch_target == 0 || config.preemption_target == 0 ||
        !config.architecture_name) return false;

    scheduler::Config schedulerConfig = {
        config.allocate_pages,
        config.stack_pages,
        config.cooperative_switch_target,
        config.preemption_target
    };
    if (!scheduler::initialize(schedulerConfig)) return false;
    if (config.log) {
        config.log("[guideXOS] common kernel entry: OK\n");
        config.log("[guideXOS] architecture interface: ");
        config.log(config.architecture_name);
        config.log("\n");
    }
    return true;
}

} // namespace common
} // namespace kernel

