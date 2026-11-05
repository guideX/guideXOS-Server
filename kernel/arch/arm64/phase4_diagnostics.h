#pragma once

#include <stdint.h>

enum Phase4DiagnosticStage : uint32_t {
    PHASE4_DIAG_NONE = 0,
    PHASE4_DIAG_SCHED_START_ENTER = 1,
    PHASE4_DIAG_SCHED_START_PRE_IRQ_RESTORE = 2,
    PHASE4_DIAG_SCHED_START_POST_IRQ_RESTORE = 3,
    PHASE4_DIAG_SCHED_START_RETURN = 4,
    PHASE4_DIAG_TIMER_ENABLED = 5,
    PHASE4_DIAG_FS_OPEN_BEGIN = 10,
    PHASE4_DIAG_FS_OPEN_LOGGED = 11,
    PHASE4_DIAG_FS_HANDLE_PUBLISHED = 12,
    PHASE4_DIAG_FS_OPEN_RETURNED = 13,
    PHASE4_DIAG_IRQ_ENTRY = 20,
    PHASE4_DIAG_IRQ_ACKED = 21,
    PHASE4_DIAG_IRQ_CALLBACK_DONE = 22,
    PHASE4_DIAG_IRQ_EOI_DONE = 23,
    PHASE4_DIAG_CONTINUATION_RESUMED = 30
};

struct Phase4DiagnosticRecord {
    uint64_t magic;
    uint32_t version;
    uint32_t size;
    volatile uint32_t sequence;
    volatile uint32_t stage;
    volatile uint32_t irq_origin_stage;
    volatile uint32_t irq_count;
    volatile uint32_t timer_ack_count;
    volatile uint32_t timer_eoi_count;
    volatile uint32_t invalid_ack_count;
    volatile uint32_t last_irq_id;
    volatile uint32_t last_acknowledgement;
    volatile uint32_t interrupted_task_id;
    volatile uint32_t current_task_id;
    volatile uint32_t current_task_state;
    volatile uint32_t runnable_tasks;
    volatile uint32_t task_count;
    volatile uint32_t destination_valid;
    volatile uint64_t current_task_pointer;
    volatile uint64_t interrupted_elr;
    volatile uint64_t interrupted_spsr;
    volatile uint64_t interrupted_sp;
    volatile uint64_t daif;
    volatile uint64_t destination_sp;
    volatile uint64_t destination_pc;
    volatile uint64_t destination_pstate;
    volatile uint64_t timer_ticks;
    volatile uint64_t context_switches;
    volatile uint64_t preemptions;
    volatile uint64_t architectural_timer_count;
    volatile uint64_t filesystem_reads;
    volatile uint64_t filesystem_enumerations;
    volatile uint64_t filesystem_failures;
    volatile uint32_t filesystem_stage;
    volatile uint32_t filesystem_handle;
    volatile uint32_t unexpected_irqs;
    volatile uint32_t exceptions;
};

extern "C" volatile Phase4DiagnosticRecord g_phase4_diagnostic_record;
extern "C" void phase4_diag_stage(uint32_t stage);
extern "C" void phase4_diag_irq_entry(void* frame);
extern "C" void phase4_diag_irq_ack(uint32_t irq, uint32_t acknowledgement);
extern "C" void phase4_diag_irq_context(void* next_context);
extern "C" void phase4_diag_irq_eoi();
extern "C" void phase4_diag_checkpoint(uint32_t stage);
