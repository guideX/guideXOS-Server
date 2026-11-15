#pragma once

#include <stdint.h>
#include "studio_elf_image.h"

#define GXOS_AARCH64_EL0_PAGE_SIZE UINT64_C(4096)
#define GXOS_AARCH64_EL0_USER_BASE UINT64_C(0x0000400000000000)
#define GXOS_AARCH64_EL0_USER_WINDOW_SIZE UINT64_C(0x00200000)
#define GXOS_AARCH64_EL0_USER_LIMIT (GXOS_AARCH64_EL0_USER_BASE + GXOS_AARCH64_EL0_USER_WINDOW_SIZE)
#define GXOS_AARCH64_EL0_EXIT_SERVICE UINT64_C(1)
#define GXOS_AARCH64_EL0_SVC_IMMEDIATE UINT64_C(0)
#define GXOS_AARCH64_EL0_CONTEXT_SP_OFFSET 248
#define GXOS_AARCH64_EL0_CONTEXT_PC_OFFSET 256
#define GXOS_AARCH64_EL0_CONTEXT_PSTATE_OFFSET 264
#define GXOS_AARCH64_EL0_CONTEXT_STATE_OFFSET 288
#define GXOS_AARCH64_EL0_CONTEXT_EXIT_OFFSET 296
#define GXOS_AARCH64_EL0_CONTEXT_KERNEL_SP_OFFSET 344
#define GXOS_AARCH64_EL0_VECTOR_LOWER_SYNC_A64 5
#define GXOS_AARCH64_EL0_VECTOR_LOWER_IRQ_A64 6

enum gxos_aarch64_el0_page_flags {
    GXOS_AARCH64_EL0_PAGE_READ = 1,
    GXOS_AARCH64_EL0_PAGE_WRITE = 2,
    GXOS_AARCH64_EL0_PAGE_EXECUTE = 4,
    GXOS_AARCH64_EL0_PAGE_STACK = 8
};

enum gxos_aarch64_el0_space_state {
    GXOS_AARCH64_EL0_SPACE_EMPTY = 0,
    GXOS_AARCH64_EL0_SPACE_CREATED = 1,
    GXOS_AARCH64_EL0_SPACE_ACTIVE = 2,
    GXOS_AARCH64_EL0_SPACE_TERMINAL = 3,
    GXOS_AARCH64_EL0_SPACE_CLEANING = 4,
    GXOS_AARCH64_EL0_SPACE_CLEANED = 5
};

enum gxos_aarch64_el0_context_state {
    GXOS_AARCH64_EL0_CONTEXT_EMPTY = 0,
    GXOS_AARCH64_EL0_CONTEXT_PREPARED = 1,
    GXOS_AARCH64_EL0_CONTEXT_RUNNING = 2,
    GXOS_AARCH64_EL0_CONTEXT_EXITED = 3,
    GXOS_AARCH64_EL0_CONTEXT_FAULTED = 4,
    GXOS_AARCH64_EL0_CONTEXT_PREEMPTED = 5,
    GXOS_AARCH64_EL0_CONTEXT_TIMED_OUT = 6,
    GXOS_AARCH64_EL0_CONTEXT_CANCELLED = 7,
    GXOS_AARCH64_EL0_CONTEXT_START_FAILED = 8
};

enum gxos_aarch64_el0_fault_reason {
    GXOS_AARCH64_EL0_FAULT_NONE = 0,
    GXOS_AARCH64_EL0_FAULT_UNSUPPORTED_SVC = 1,
    GXOS_AARCH64_EL0_FAULT_INSTRUCTION_ABORT = 2,
    GXOS_AARCH64_EL0_FAULT_DATA_ABORT = 3,
    GXOS_AARCH64_EL0_FAULT_UNEXPECTED_SYNC = 4,
    GXOS_AARCH64_EL0_FAULT_UNEXPECTED_INTERRUPT = 5,
    GXOS_AARCH64_EL0_FAULT_BAD_EXCEPTION_STATE = 6
};

enum gxos_aarch64_el0_negative_probe {
    GXOS_AARCH64_EL0_PROBE_KERNEL_DATA = 1,
    GXOS_AARCH64_EL0_PROBE_DEVICE_MMIO = 2
};

enum gxos_aarch64_el0_qualification_fixture {
    GXOS_AARCH64_EL0_FIXTURE_FINITE_PREEMPTION = 1,
    GXOS_AARCH64_EL0_FIXTURE_NONCOOPERATIVE = 2,
    GXOS_AARCH64_EL0_FIXTURE_CANCELLATION = 3,
    GXOS_AARCH64_EL0_FIXTURE_PROTECTED_MEMORY_FAULT = 4
};

struct gxos_aarch64_el0_negative_probe_result {
    uint64_t target_address;
    uint64_t kernel_descriptor;
    uint64_t child_identity;
    uint64_t owner_id;
    uint64_t context_state;
    uint64_t fault_reason;
    uint64_t esr;
    uint64_t elr;
    uint64_t spsr;
    uint64_t far;
    uint64_t read_value;
    uint64_t data_pages_before_cleanup;
    uint64_t table_pages_before_cleanup;
    uint8_t target_access_succeeded;
    uint8_t kernel_root_restored;
    uint8_t cleanup_succeeded;
};

struct gxos_aarch64_el0_mapping {
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t flags;
    uint64_t owner_id;
};

// Fixed-capacity metadata keeps allocation and teardown bounded in the
// freestanding kernel. table_pages[0] is the private root; the remaining
// entries are private user-range page-table pages. Kernel subtrees are shared
// read-only by convention and retain their original EL1-only descriptors.
struct gxos_aarch64_el0_address_space {
    uint64_t identity;
    uint64_t owner_id;
    uint64_t kernel_root;
    uint64_t root;
    uint64_t state;
    uint64_t cleanup_state;
    uint64_t table_count;
    uint64_t page_count;
    uint64_t mapping_count;
    uint64_t table_pages[8];
    uint64_t owned_pages[16];
    gxos_aarch64_el0_mapping mappings[16];
};

// x0-x30 are first so the lower-EL exception bridge can pass this record
// without translating or truncating the architectural register frame.
struct gxos_aarch64_el0_context {
    uint64_t x[31];
    uint64_t sp_el0;
    uint64_t entry_pc;
    uint64_t pstate;
    uint64_t address_space_id;
    uint64_t owner_id;
    uint64_t state;
    uint64_t exit_value;
    uint64_t fault_esr;
    uint64_t fault_elr;
    uint64_t fault_spsr;
    uint64_t fault_far;
    uint64_t fault_reason;
    uint64_t kernel_sp;
};

struct gxos_aarch64_el0_task_result {
    uint64_t execution_id;
    uint64_t owner_id;
    uint64_t state;
    uint64_t result_kind;
    uint64_t result_value;
    // Monotonic-counter deltas charged for child schedule slices, sampled
    // around EL0 entry/resume and timer/final-return boundaries. This includes
    // small exception-path latency and is not wall time or a cycle count.
    uint64_t scheduled_counter_ticks;
    uint64_t budget_counter_ticks;
    uint64_t address_space_id;
    uint64_t address_space_owner_id;
    uint64_t scheduler_task_id;
    uint64_t kernel_stack_base;
    uint64_t kernel_stack_size;
    uint64_t user_stack_pointer;
    uint64_t user_page_count;
    uint64_t page_table_page_count;
    uint64_t preemption_count;
    uint64_t el0_entry_count;
    uint64_t el0_resume_count;
    uint64_t el0_timer_interrupt_count;
    uint64_t saved_x0;
    uint64_t saved_x9;
    uint64_t fault_esr;
    uint64_t fault_elr;
    uint64_t fault_spsr;
    uint64_t fault_far;
    uint64_t kernel_root_restored;
    uint64_t cancel_requested;
    uint64_t resources_cleaned;
    // Internal qualification observations. These are kernel-internal fields,
    // not part of the public Studio adapter ABI.
    uint64_t compiled_entry_point;
    uint64_t el0_initial_pc;
    uint64_t image_entry_descriptor;
    uint64_t trampoline_descriptor;
    uint64_t user_stack_descriptor;
    uint64_t lower_stack_guard_descriptor;
    uint64_t upper_stack_guard_descriptor;
};

// Exact identity returned by the resident Build service. The owner token is
// the Phase 9 application runtime identity; owner_id below remains the
// current scheduler kernel task that owns the asynchronous child lifecycle.
struct gxos_aarch64_studio_build_identity {
    uint64_t owner_identity;
    uint64_t build_generation;
    uint64_t artifact_size;
    const char* project_id;
    const char* artifact_path;
    const char* package_path;
    uint64_t package_generation;
    const char* architecture;
    const char* abi;
    const char* source_sha256;
    const char* artifact_sha256;
};

#ifdef __cplusplus
extern "C" {
#endif

uint8_t phase2_el0_address_space_create(gxos_aarch64_el0_address_space* space,
                                        uint64_t owner_id);
uint8_t phase2_el0_allocate_page(gxos_aarch64_el0_address_space* space,
                                 uint64_t owner_id, uint64_t virtual_address,
                                 uint64_t flags, uint64_t* physical_address);
uint8_t phase2_el0_map_owned_page(gxos_aarch64_el0_address_space* space,
                                  uint64_t owner_id, uint64_t virtual_address,
                                  uint64_t physical_address, uint64_t flags);
uint8_t phase2_el0_copy_code(gxos_aarch64_el0_address_space* space,
                             uint64_t owner_id, uint64_t virtual_address,
                             const uint8_t* image, uint64_t image_size);
uint8_t phase2_el0_copy_owned_memory(gxos_aarch64_el0_address_space* space,
                                     uint64_t owner_id, uint64_t virtual_address,
                                     const uint8_t* bytes, uint64_t byte_count);
uint8_t phase2_el0_query_descriptor(const gxos_aarch64_el0_address_space* space,
                                    uint64_t virtual_address,
                                    uint64_t* descriptor);
uint8_t phase2_el0_validate_entry(const gxos_aarch64_el0_address_space* space,
                                  uint64_t entry_pc);
uint8_t phase2_el0_validate_stack(const gxos_aarch64_el0_address_space* space,
                                  uint64_t stack_pointer);
uint8_t phase2_el0_validate_context(const gxos_aarch64_el0_address_space* space,
                                    const gxos_aarch64_el0_context* context,
                                    uint64_t expected_owner_id);
uint8_t phase2_el0_begin_context(gxos_aarch64_el0_address_space* space,
                                gxos_aarch64_el0_context* context,
                                uint64_t expected_owner_id);
uint8_t phase2_el0_activate(gxos_aarch64_el0_address_space* space);
uint8_t phase2_el0_deactivate(gxos_aarch64_el0_address_space* space);
uint8_t phase2_el0_suspend_for_schedule(uint64_t* register_frame,
                                        uint64_t elr, uint64_t spsr,
                                        uint64_t sp_el0);
uint8_t phase2_el0_resume_for_schedule(
    gxos_aarch64_el0_address_space* space,
    gxos_aarch64_el0_context* context, uint64_t expected_owner_id);
uint8_t phase2_el0_terminate_preempted(
    gxos_aarch64_el0_address_space* space,
    gxos_aarch64_el0_context* context, uint64_t expected_owner_id,
    uint64_t terminal_state);
uint8_t phase2_el0_cleanup(gxos_aarch64_el0_address_space* space);
uint8_t phase2_el0_dispatch_exception(uint64_t* register_frame,
                                      uint64_t vector_class,
                                      uint64_t esr, uint64_t elr,
                                      uint64_t spsr, uint64_t far,
                                      gxos_aarch64_el0_context** return_context);
uint8_t phase2_el0_run_qualification(uint64_t* observed_exit_value,
                                      uint64_t* fault_reason);
uint8_t phase2_el0_run_negative_probe(uint64_t probe_kind,
                                      uint64_t device_address,
                                      gxos_aarch64_el0_negative_probe_result* result);
uint8_t phase2_el0_task_create(const uint8_t* trusted_image,
                               uint64_t image_size, uint64_t owner_id,
                               uint64_t budget_microseconds,
                               uint64_t* execution_id);
uint8_t phase2_el0_task_create_studio_elf(
    const uint8_t* validated_elf, uint64_t elf_size, uint64_t owner_id,
    uint64_t budget_microseconds, uint64_t* execution_id);
uint8_t phase2_el0_task_create_studio_build(
    const gxos_aarch64_studio_build_identity* identity, uint64_t owner_id,
    uint64_t budget_microseconds, uint64_t* execution_id);
uint8_t phase2_el0_task_start(uint64_t execution_id, uint64_t owner_id);
uint8_t phase2_el0_task_query(uint64_t execution_id, uint64_t owner_id,
                              gxos_aarch64_el0_task_result* result);
uint8_t phase2_el0_task_cancel(uint64_t execution_id, uint64_t owner_id);
uint8_t phase2_el0_task_reap(uint64_t execution_id, uint64_t owner_id);
uint8_t phase2_el0_task_create_fixture(uint64_t fixture_kind,
                                       uint64_t owner_id,
                                       uint64_t budget_microseconds,
                                       uint64_t* execution_id);
uint8_t phase2_el0_task_active_context(
    const gxos_aarch64_el0_address_space** space,
    gxos_aarch64_el0_context** context);
uint64_t phase2_el0_task_clock_now();
uint8_t phase2_el0_task_runtime_ready();
uint8_t phase2_el0_task_finish_preempted(void* scheduler_task);
uint8_t phase2_el0_task_finish_exception(void* scheduler_task);

#ifdef __cplusplus
}
#endif
