#ifndef KERNEL_TIME_H
#define KERNEL_TIME_H

#include "kernel/types.h"

namespace kernel {
namespace time {

struct DateTime {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
};

// Returns true only when a real platform time source provided a valid value.
bool get_current_datetime(DateTime& out);

} // namespace time
} // namespace kernel

#endif // KERNEL_TIME_H
