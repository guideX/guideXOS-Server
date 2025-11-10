#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <utility>

// Simplified page allocator mirroring C# Allocator for server (no paging hardware mapping here)
namespace gxos {
    static const uint64_t PageSize = 4096;
    struct AllocStat { uint64_t pagesInUse=0; uint64_t peakPages=0; };
    enum class AllocTag : uint8_t { Unknown=0, ThreadMeta, ThreadStack, ExecImage, Image, FileBuffer, Temp, Count };

    struct BlockHeader { uint32_t pages; AllocTag tag; uint64_t pid; };

    class Allocator {
    public:
        static void init(size_t totalBytes);
        static void* alloc(size_t bytes, AllocTag tag=AllocTag::Unknown);
        static void* realloc(void* ptr, size_t bytes);
        static void free(void* ptr);
        static uint64_t bytesInUse();
        static uint64_t peakBytes();
        static uint64_t tagBytes(AllocTag tag);
        // Per-process attribution
        static void setCurrentPid(uint64_t pid);
        static uint64_t currentPid();
        static uint64_t pidBytes(uint64_t pid);
        static std::vector<std::pair<uint64_t,uint64_t>> listPidBytes();
    private:
        static uint8_t* g_heap;
        static size_t g_pagesTotal;
        static std::vector<uint8_t> g_pageTags; // 0=free else first page stores header
        static std::mutex g_lock;
        static AllocStat g_stat;
        static uint64_t g_tagPageCount[(size_t)AllocTag::Count];
        static std::unordered_map<uint64_t,uint64_t> g_pidPageCount; // pages per pid
        static std::mutex g_pidMu;
        static thread_local uint64_t t_pid;
    };
}
