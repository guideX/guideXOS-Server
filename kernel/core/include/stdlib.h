#pragma once

#include "kernel/types.h"

#ifdef __cplusplus
extern "C" {
#endif

void* calloc(size_t nmemb, size_t size);
void free(void* ptr);
void exit(int status);

#ifdef __cplusplus
}
#endif
