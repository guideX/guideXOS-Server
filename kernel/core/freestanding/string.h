#pragma once

// Small C library declaration surface used by the freestanding graphics
// decoder.  Implementations are supplied by kernel/core/cxx_runtime.cpp.
typedef __SIZE_TYPE__ size_t;

#ifdef __cplusplus
extern "C" {
#endif
void* memcpy(void* destination, const void* source, size_t count);
void* memset(void* destination, int value, size_t count);
void* memmove(void* destination, const void* source, size_t count);
int memcmp(const void* left, const void* right, size_t count);
size_t strlen(const char* value);
int strcmp(const char* left, const char* right);
int strncmp(const char* left, const char* right, size_t count);
char* strstr(const char* haystack, const char* needle);
#ifdef __cplusplus
}
#endif
