#pragma once

#include <stdint.h>

#ifndef __cdecl
#define __cdecl
#endif

// The three fields are the native Win64 RUNTIME_FUNCTION representation
// retained by the guideXOS kernel linker.  The unwind primitive deliberately
// accepts a pointer-free context ABI so it can be reused by both the NativeAOT
// managed image and the converted kernel image.
struct GuidexosRuntimeFunction {
    uint32_t beginAddress;
    uint32_t endAddress;
    uint32_t unwindData;
};

extern "C" void* __cdecl guideXosRtlVirtualUnwind(
    uint32_t handlerType, uint64_t imageBase, uint64_t controlPc,
    void* functionEntry, void* context, void** handlerData,
    uint64_t* establisherFrame, void* contextPointers);
