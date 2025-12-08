//
// Kernel Type Definitions
// Replacement for <cstdint> in freestanding environment
//
// Copyright (c) 2024 guideX
//

#pragma once

// Fixed-width integer types for freestanding environment
typedef signed char        int8_t;
typedef unsigned char      uint8_t;
typedef signed short       int16_t;
typedef unsigned short     uint16_t;
typedef signed int         int32_t;
typedef unsigned int       uint32_t;
typedef signed long long   int64_t;
typedef unsigned long long uint64_t;

// Pointer-sized types
#if defined(__x86_64__) || defined(__amd64__) || defined(_M_X64)
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

// NULL pointer
#ifndef NULL
    #define NULL ((void*)0)
#endif

// nullptr for C++
#ifndef nullptr
    #define nullptr NULL
#endif
