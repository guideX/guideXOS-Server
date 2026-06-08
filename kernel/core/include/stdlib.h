#pragma once

#include "kernel/types.h"

#ifdef __cplusplus
extern "C" {
#endif

void* malloc(size_t size);
void* calloc(size_t nmemb, size_t size);
void free(void* ptr);
void* realloc(void* ptr, size_t size);
void exit(int status);

#ifdef __cplusplus
}
#endif
