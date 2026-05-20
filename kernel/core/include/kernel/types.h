//
// Kernel Type Definitions
// Replacement for <cstdint> in freestanding environment
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

// In a freestanding GCC environment, <stdint.h> and <stddef.h> are among
// the few guaranteed headers. Use them to avoid typedef conflicts with GCC
// internal headers (e.g., stdint-gcc.h using 'long int' for int32_t).
// <stddef.h> provides size_t, ptrdiff_t, and NULL for all GCC targets.
#if defined(__GNUC__) || defined(__clang__)
#include <stdint.h>
#include <stddef.h>
#else
// MSVC or other compilers without freestanding <stdint.h>
typedef signed char        int8_t;
typedef unsigned char      uint8_t;
typedef signed short       int16_t;
typedef unsigned short     uint16_t;
typedef signed int         int32_t;
typedef unsigned int       uint32_t;
typedef signed long long   int64_t;
typedef unsigned long long uint64_t;
#endif

// Pointer-sized types (only needed for MSVC; GCC/Clang get them from stddef.h)
#if !defined(__GNUC__) && !defined(__clang__)
#if defined(_M_X64) || defined(_M_IA64)
    typedef uint64_t uintptr_t;
    typedef int64_t  intptr_t;
    typedef uint64_t size_t;
    typedef int64_t  ssize_t;
#else
    typedef uint32_t uintptr_t;
    typedef int32_t  intptr_t;
    typedef uint32_t size_t;
    typedef int32_t  ssize_t;
#endif
#endif

// NULL pointer for non-GCC/Clang compilers (GCC/Clang get it from stddef.h)
#if !defined(__GNUC__) && !defined(__clang__)
#ifndef NULL
    #ifdef __cplusplus
        #define NULL 0
    #else
        #define NULL ((void*)0)
    #endif
#endif
#endif

// Note: C++11 and later have nullptr as a keyword, don't redefine it
