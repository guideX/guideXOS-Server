#ifndef KERNEL_SERIAL_DEBUG_H
#define KERNEL_SERIAL_DEBUG_H
namespace kernel { namespace serial {
void puts(const char*);
void putc(char);
void put_hex32(unsigned int);
} }
#endif
