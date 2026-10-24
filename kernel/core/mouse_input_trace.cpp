#include "include/kernel/mouse_input_trace.h"
#include "include/kernel/serial_debug.h"

namespace kernel {
namespace mouse_trace {

#if defined(GXOS_PS2_CLICK_TRACE)

static const uint32_t kRecordCapacity = 256;

struct Record {
    uint32_t eventId;
    Stage stage;
    int32_t x;
    int32_t y;
    uint8_t buttons;
    int32_t valueA;
    int32_t valueB;
    uint32_t targetId;
    int32_t result;
};

static Record s_mainRecords[kRecordCapacity];
static Record s_irqRecords[kRecordCapacity];
static volatile uint32_t s_mainRecordCount = 0;
static volatile uint32_t s_irqRecordCount = 0;
static volatile uint32_t s_mainDroppedCount = 0;
static volatile uint32_t s_irqDroppedCount = 0;
static uint32_t s_flushedMainCount = 0;
static uint32_t s_flushedIrqCount = 0;
static uint32_t s_flushedMainDroppedCount = 0;
static uint32_t s_flushedIrqDroppedCount = 0;

static void put_decimal(int32_t value)
{
    char buffer[12];
    uint32_t index = sizeof(buffer) - 1u;
    buffer[index] = '\0';
    bool negative = value < 0;
    uint32_t magnitude = negative
        ? static_cast<uint32_t>(-(static_cast<int64_t>(value)))
        : static_cast<uint32_t>(value);
    do {
        buffer[--index] = static_cast<char>('0' + (magnitude % 10u));
        magnitude /= 10u;
    } while (magnitude != 0u && index > 0u);
    if (negative && index > 0u) buffer[--index] = '-';
    serial::puts(&buffer[index]);
}

static const char* stage_name(Stage stage)
{
    switch (stage) {
        case Ps2Byte: return "ps2-byte";
        case Ps2Packet: return "ps2-packet";
        case ButtonTransition: return "button";
        case PointerMapped: return "pointer-map";
        case DesktopDispatch: return "desktop-dispatch";
        case WindowHitTest: return "window-hit";
        case WidgetHitTest: return "widget-hit";
        case ClickResult: return "click-result";
        default: return "unknown";
    }
}

static void append_record(Record* records, volatile uint32_t& count,
                          volatile uint32_t& dropped, uint32_t eventId,
                          Stage stage, int32_t x, int32_t y, uint8_t buttons,
                          int32_t valueA, int32_t valueB,
                          uint32_t targetId, int32_t result)
{
    const uint32_t index = count;
    if (index >= kRecordCapacity) {
        ++dropped;
        return;
    }

    Record& record = records[index];
    record.eventId = eventId;
    record.stage = stage;
    record.x = x;
    record.y = y;
    record.buttons = buttons;
    record.valueA = valueA;
    record.valueB = valueB;
    record.targetId = targetId;
    record.result = result;
    // The interrupt and main-loop producers have independent fixed buffers.
    // Publish only after the complete record is written.
    count = index + 1u;
}

void record(uint32_t eventId, Stage stage, int32_t x, int32_t y,
            uint8_t buttons, int32_t valueA, int32_t valueB,
            uint32_t targetId, int32_t result)
{
    append_record(s_mainRecords, s_mainRecordCount, s_mainDroppedCount,
                  eventId, stage, x, y, buttons, valueA, valueB,
                  targetId, result);
}

void record_irq(uint32_t eventId, Stage stage, int32_t x, int32_t y,
                uint8_t buttons, int32_t valueA, int32_t valueB,
                uint32_t targetId, int32_t result)
{
    append_record(s_irqRecords, s_irqRecordCount, s_irqDroppedCount,
                  eventId, stage, x, y, buttons, valueA, valueB,
                  targetId, result);
}

void flush()
{
    uint32_t irqCount = s_irqRecordCount;
    if (irqCount > kRecordCapacity) irqCount = kRecordCapacity;
    while (s_flushedIrqCount < irqCount) {
        const Record& record = s_irqRecords[s_flushedIrqCount++];
        serial::puts("[MOUSE-TRACE] id=");
        serial::put_hex32(record.eventId);
        serial::puts(" stage=");
        serial::puts(stage_name(record.stage));
        serial::puts(" xy=");
        put_decimal(record.x);
        serial::putc(',');
        put_decimal(record.y);
        serial::puts(" buttons=");
        serial::put_hex8(record.buttons);
        serial::puts(" a=");
        put_decimal(record.valueA);
        serial::puts(" b=");
        put_decimal(record.valueB);
        serial::puts(" target=");
        serial::put_hex32(record.targetId);
        serial::puts(" result=");
        put_decimal(record.result);
        serial::putc('\n');
    }

    uint32_t mainCount = s_mainRecordCount;
    if (mainCount > kRecordCapacity) mainCount = kRecordCapacity;
    while (s_flushedMainCount < mainCount) {
        const Record& record = s_mainRecords[s_flushedMainCount++];
        serial::puts("[MOUSE-TRACE] id=");
        serial::put_hex32(record.eventId);
        serial::puts(" stage=");
        serial::puts(stage_name(record.stage));
        serial::puts(" xy=");
        put_decimal(record.x);
        serial::putc(',');
        put_decimal(record.y);
        serial::puts(" buttons=");
        serial::put_hex8(record.buttons);
        serial::puts(" a=");
        put_decimal(record.valueA);
        serial::puts(" b=");
        put_decimal(record.valueB);
        serial::puts(" target=");
        serial::put_hex32(record.targetId);
        serial::puts(" result=");
        put_decimal(record.result);
        serial::putc('\n');
    }

    const uint32_t irqDropped = s_irqDroppedCount;
    if (irqDropped != s_flushedIrqDroppedCount) {
        s_flushedIrqDroppedCount = irqDropped;
        serial::puts("[MOUSE-TRACE] irq-overflow dropped=");
        serial::put_hex32(irqDropped);
        serial::putc('\n');
    }
    const uint32_t mainDropped = s_mainDroppedCount;
    if (mainDropped != s_flushedMainDroppedCount) {
        s_flushedMainDroppedCount = mainDropped;
        serial::puts("[MOUSE-TRACE] overflow dropped=");
        serial::put_hex32(mainDropped);
        serial::putc('\n');
    }
}

#else

void record(uint32_t, Stage, int32_t, int32_t, uint8_t, int32_t, int32_t,
            uint32_t, int32_t) {}
void record_irq(uint32_t, Stage, int32_t, int32_t, uint8_t, int32_t, int32_t,
                uint32_t, int32_t) {}
void flush() {}

#endif

} // namespace mouse_trace
} // namespace kernel
