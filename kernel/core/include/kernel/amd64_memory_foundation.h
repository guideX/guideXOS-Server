#pragma once

namespace guideXOS { struct BootInfo; }

namespace kernel {
namespace memory {

// Boot-time opt-in for the bounded protected-task frame arena. A failure
// leaves the existing ring-0 application path available and CPL3 unavailable.
bool initialize_amd64_task_memory(const guideXOS::BootInfo* bootInfo);

} // namespace memory
} // namespace kernel
