#include <stddef.h>

void* operator new(size_t, void* address) { return address; }
void* operator new[](size_t, void* address) { return address; }
void operator delete(void*, void*) noexcept {}
void operator delete[](void*, void*) noexcept {}

extern "C" void* memset(void* destination, int value, size_t bytes)
{
    unsigned char* output = static_cast<unsigned char*>(destination);
    for (size_t i = 0; i < bytes; ++i) output[i] = static_cast<unsigned char>(value);
    return destination;
}

extern "C" void* memcpy(void* destination, const void* source, size_t bytes)
{
    unsigned char* output = static_cast<unsigned char*>(destination);
    const unsigned char* input = static_cast<const unsigned char*>(source);
    for (size_t i = 0; i < bytes; ++i) output[i] = input[i];
    return destination;
}
