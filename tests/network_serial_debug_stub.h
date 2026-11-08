#ifndef KERNEL_SERIAL_DEBUG_H
#define KERNEL_SERIAL_DEBUG_H

#include <stdint.h>

namespace kernel { namespace serial {
void puts(const char*);
void putc(char);
void put_hex8(uint8_t);
void put_hex16(uint16_t);
void put_hex32(uint32_t);
} }

#endif
