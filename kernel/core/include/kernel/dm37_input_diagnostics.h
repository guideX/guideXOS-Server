#ifndef KERNEL_DM37_INPUT_DIAGNOSTICS_H
#define KERNEL_DM37_INPUT_DIAGNOSTICS_H

#include "kernel/types.h"
#include "kernel/serial_debug.h"

namespace kernel { namespace dm37_input_diagnostics {

// A full disk workflow can cross dozens of kernel, compositor, and widget
// handoffs. Keep enough bounded records for one focused run while avoiding
// unbounded serial output or hot-path allocation.
static const uint16_t MAX_TRACE_LINES = 512;
static const uint8_t MAX_POINTER_MOTION_LINES = 16;

struct TraceBudget {
    uint32_t nextSequence;
    uint16_t emittedLines;
    uint8_t emittedPointerMotionLines;
    uint32_t droppedLines;
    uint32_t droppedPointerMotionLines;

    static void increment_saturated(uint32_t* counter)
    {
        uint32_t observed = __atomic_load_n(counter, __ATOMIC_RELAXED);
        for (;;) {
            if (observed == 0xFFFFFFFFu) return;
            const uint32_t next = observed + 1u;
            if (__atomic_compare_exchange_n(counter, &observed, next, false,
                    __ATOMIC_RELAXED, __ATOMIC_RELAXED))
                return;
        }
    }

    uint32_t begin_event()
    {
        uint32_t observed = __atomic_load_n(&nextSequence, __ATOMIC_RELAXED);
        for (;;) {
            uint32_t next = observed + 1u;
            if (next == 0) next = 1;
            if (__atomic_compare_exchange_n(&nextSequence, &observed, next,
                    false, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
                return next;
        }
    }

    bool claim_line(bool pointerMotion)
    {
        if (pointerMotion) {
            uint8_t observedMotion = __atomic_load_n(
                &emittedPointerMotionLines, __ATOMIC_RELAXED);
            for (;;) {
                if (observedMotion >= MAX_POINTER_MOTION_LINES) {
                    increment_saturated(&droppedPointerMotionLines);
                    return false;
                }
                if (__atomic_compare_exchange_n(&emittedPointerMotionLines,
                        &observedMotion,
                        static_cast<uint8_t>(observedMotion + 1u), false,
                        __ATOMIC_RELAXED, __ATOMIC_RELAXED))
                    break;
            }
        }

        uint16_t observedLines = __atomic_load_n(&emittedLines,
                                                  __ATOMIC_RELAXED);
        for (;;) {
            if (observedLines >= MAX_TRACE_LINES) {
                if (pointerMotion)
                    __atomic_fetch_sub(&emittedPointerMotionLines,
                                       static_cast<uint8_t>(1),
                                       __ATOMIC_RELAXED);
                increment_saturated(&droppedLines);
                return false;
            }
            if (__atomic_compare_exchange_n(&emittedLines, &observedLines,
                    static_cast<uint16_t>(observedLines + 1u), false,
                    __ATOMIC_RELAXED, __ATOMIC_RELAXED))
                return true;
        }
    }
};

struct TraceRecord {
    volatile uint32_t ready;
    const char* stage;
    uint32_t sequence;
    uint32_t first;
    uint32_t second;
};

inline bool is_trace_key(uint32_t key)
{
    return key == 9 || key == 10 || key == 13 || key == 27 || key == 32 ||
           key == 0x102 || key == 0x103 || key == 0x113 ||
           key == '`' || key == '~';
}

inline TraceBudget& budget()
{
    static TraceBudget state = {};
    return state;
}

inline bool& postcheck_checkpoint_armed_storage()
{
    static bool armed = false;
    return armed;
}

inline bool capture_armed()
{
#if defined(GXOS_DM37_INPUT_DIAGNOSTICS)
#if defined(GXOS_DM37_INPUT_DIAGNOSTICS_POSTCHECK_ONLY)
    return __atomic_load_n(&postcheck_checkpoint_armed_storage(),
                           __ATOMIC_ACQUIRE);
#else
    return true;
#endif
#else
    return false;
#endif
}

inline bool should_trace_key(uint32_t key)
{
#if defined(GXOS_DM37_INPUT_DIAGNOSTICS_POSTCHECK_ONLY)
    return capture_armed() && is_trace_key(key);
#else
    return is_trace_key(key);
#endif
}

inline TraceRecord* records()
{
    static TraceRecord storage[MAX_TRACE_LINES] = {};
    return storage;
}

inline uint32_t& reserved_record_count()
{
    static uint32_t count = 0;
    return count;
}

inline uint32_t& next_record_to_flush()
{
    static uint32_t next = 0;
    return next;
}

inline bool& loss_summary_emitted_storage()
{
    static bool emitted = false;
    return emitted;
}

inline bool loss_summary_pending(const TraceBudget& traceBudget,
                                 uint32_t flushed, uint32_t reserved)
{
    return flushed == reserved &&
        (__atomic_load_n(&traceBudget.droppedLines, __ATOMIC_RELAXED) != 0 ||
         __atomic_load_n(&traceBudget.droppedPointerMotionLines,
                         __ATOMIC_RELAXED) != 0);
}

inline void arm_postcheck_focus_checkpoint()
{
#if defined(GXOS_DM37_INPUT_DIAGNOSTICS) && \
    defined(GXOS_DM37_INPUT_DIAGNOSTICS_POSTCHECK_ONLY)
    if (capture_armed()) return;

    TraceBudget& traceBudget = budget();
    __atomic_store_n(&traceBudget.emittedLines, static_cast<uint16_t>(0),
                     __ATOMIC_RELAXED);
    __atomic_store_n(&traceBudget.emittedPointerMotionLines,
                     static_cast<uint8_t>(0), __ATOMIC_RELAXED);
    __atomic_store_n(&traceBudget.droppedLines, static_cast<uint32_t>(0),
                     __ATOMIC_RELAXED);
    __atomic_store_n(&traceBudget.droppedPointerMotionLines,
                     static_cast<uint32_t>(0), __ATOMIC_RELAXED);
    __atomic_store_n(&reserved_record_count(), static_cast<uint32_t>(0),
                     __ATOMIC_RELAXED);
    __atomic_store_n(&next_record_to_flush(), static_cast<uint32_t>(0),
                     __ATOMIC_RELAXED);
    __atomic_store_n(&loss_summary_emitted_storage(), false,
                     __ATOMIC_RELAXED);
    TraceRecord* const traceRecords = records();
    for (uint32_t i = 0; i < MAX_TRACE_LINES; ++i)
        __atomic_store_n(&traceRecords[i].ready, static_cast<uint32_t>(0),
                         __ATOMIC_RELAXED);
    __atomic_store_n(&postcheck_checkpoint_armed_storage(), true,
                     __ATOMIC_RELEASE);
#if !defined(GXOS_DM37_INPUT_DIAGNOSTICS_TEST)
    serial::puts("[DM37-IN] checkpoint=post-fat32-result-close\n");
#endif
#endif
}

inline uint32_t begin_event()
{
#if defined(GXOS_DM37_INPUT_DIAGNOSTICS)
    if (!capture_armed()) return 0;
    return budget().begin_event();
#else
    return 0;
#endif
}

inline uint32_t& current_sequence_storage()
{
    static uint32_t currentSequence = 0;
    return currentSequence;
}

inline void set_current_sequence(uint32_t sequence)
{
    // Reserve a sequence value for the single-threaded compositor handoff.
    // The value is updated by input polling, outside device interrupt handlers.
    current_sequence_storage() = sequence;
}

inline uint32_t current_sequence()
{
    return current_sequence_storage();
}

inline void emit(const char* stage, uint32_t sequence,
                 uint32_t first, uint32_t second,
                 bool pointerMotion = false)
{
#if defined(GXOS_DM37_INPUT_DIAGNOSTICS)
    if (sequence == 0 || !capture_armed() ||
        !budget().claim_line(pointerMotion)) return;
    const uint32_t slot = __atomic_fetch_add(&reserved_record_count(), 1u,
                                              __ATOMIC_RELAXED);
    if (slot >= MAX_TRACE_LINES) {
        TraceBudget::increment_saturated(&budget().droppedLines);
        return;
    }
    TraceRecord& record = records()[slot];
    record.stage = stage;
    record.sequence = sequence;
    record.first = first;
    record.second = second;
    __atomic_store_n(&record.ready, slot + 1u, __ATOMIC_RELEASE);
#else
    (void)stage;
    (void)sequence;
    (void)first;
    (void)second;
    (void)pointerMotion;
#endif
}

inline void flush(uint8_t maxLines = 2)
{
#if defined(GXOS_DM37_INPUT_DIAGNOSTICS)
    uint32_t& next = next_record_to_flush();
    TraceRecord* const traceRecords = records();
    for (uint8_t count = 0; count < maxLines &&
         next < MAX_TRACE_LINES; ++count) {
        TraceRecord& record = traceRecords[next];
        if (__atomic_load_n(&record.ready, __ATOMIC_ACQUIRE) != next + 1u)
            break;
        serial::puts("[DM37-IN] seq=");
        serial::put_hex32(record.sequence);
        serial::puts(" stage=");
        serial::puts(record.stage);
        serial::puts(" a=");
        serial::put_hex32(record.first);
        serial::puts(" b=");
        serial::put_hex32(record.second);
        serial::putc('\n');
        ++next;
    }
    const uint32_t reserved = __atomic_load_n(&reserved_record_count(),
                                               __ATOMIC_ACQUIRE);
    if (!__atomic_load_n(&loss_summary_emitted_storage(), __ATOMIC_RELAXED) &&
        loss_summary_pending(budget(), next, reserved)) {
        serial::puts("[DM37-IN] trace=incomplete dropped=0x");
        serial::put_hex32(__atomic_load_n(&budget().droppedLines,
                                           __ATOMIC_RELAXED));
        serial::puts(" pointer-motion-omitted=0x");
        serial::put_hex32(__atomic_load_n(
            &budget().droppedPointerMotionLines, __ATOMIC_RELAXED));
        serial::putc('\n');
        __atomic_store_n(&loss_summary_emitted_storage(), true,
                         __ATOMIC_RELAXED);
    }
#else
    (void)maxLines;
#endif
}

inline uint32_t pack_signed_deltas(int32_t x, int32_t y)
{
    return (static_cast<uint32_t>(static_cast<uint16_t>(x)) << 16) |
           static_cast<uint16_t>(y);
}

inline uint32_t pack_coordinates(int32_t x, int32_t y)
{
    return (static_cast<uint32_t>(static_cast<uint16_t>(x)) << 16) |
           static_cast<uint16_t>(y);
}

}} // namespace kernel::dm37_input_diagnostics

#endif
