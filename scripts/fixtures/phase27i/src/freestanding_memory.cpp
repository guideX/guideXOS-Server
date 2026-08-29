#include <stddef.h>

extern "C" void* memcpy(void* destination, const void* source, size_t bytes)
{
    unsigned char* out = static_cast<unsigned char*>(destination);
    const unsigned char* in = static_cast<const unsigned char*>(source);
    for (size_t i = 0; i < bytes; ++i) out[i] = in[i];
    return destination;
}

extern "C" void* memset(void* destination, int value, size_t bytes)
{
    unsigned char* out = static_cast<unsigned char*>(destination);
    for (size_t i = 0; i < bytes; ++i) out[i] = static_cast<unsigned char>(value);
    return destination;
}
