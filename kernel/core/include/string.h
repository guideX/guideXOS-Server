#ifndef GXOS_FREESTANDING_STRING_H
#define GXOS_FREESTANDING_STRING_H

// Project-owned declarations for the freestanding routines implemented by
// core/cxx_runtime.cpp. This header intentionally does not depend on a hosted
// C library or provide replacement implementations.
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void* memcpy(void* destination, const void* source, size_t count);
void* memset(void* destination, int value, size_t count);
void* memmove(void* destination, const void* source, size_t count);
int memcmp(const void* left, const void* right, size_t count);

size_t strlen(const char* text);
int strcmp(const char* left, const char* right);
int strncmp(const char* left, const char* right, size_t count);
char* strchr(const char* text, int character);
char* strrchr(const char* text, int character);
char* strstr(const char* text, const char* search);

#ifdef __cplusplus
}
#endif

#endif // GXOS_FREESTANDING_STRING_H
