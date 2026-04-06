#include "allocator.h"
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace gxos {
    uint8_t* Allocator::g_heap = nullptr;
    size_t Allocator::g_pagesTotal = 0;
    std::vector<uint8_t> Allocator::g_pageTags;
    std::mutex Allocator::g_lock;
    AllocStat Allocator::g_stat{};
    uint64_t Allocator::g_tagPageCount[(size_t)AllocTag::Count] = {};
    std::unordered_map<uint64_t,uint64_t> Allocator::g_pidPageCount; // pages per pid
    std::mutex Allocator::g_pidMu;
    thread_local uint64_t Allocator::t_pid = 0; // 0 = host/system

    void Allocator::init(size_t totalBytes){
        std::lock_guard<std::mutex> _g(g_lock);
        if (g_heap) return;
        g_pagesTotal = (totalBytes + PageSize - 1) / PageSize;
        g_heap = (uint8_t*)std::malloc(g_pagesTotal * PageSize);
        g_pageTags.assign(g_pagesTotal, 0);
        std::memset(g_tagPageCount, 0, sizeof(g_tagPageCount));
        g_stat = {};
    }

    void* Allocator::alloc(size_t bytes, AllocTag tag){
        if (bytes==0) bytes=1;
        size_t pages = (bytes + PageSize - 1) / PageSize;
        std::lock_guard<std::mutex> _g(g_lock);
        size_t run=0, start=0;
        for (size_t i=0;i<g_pagesTotal;i++){
            if (g_pageTags[i]==0){ if (run==0) start=i; if (++run>=pages) { break; } }
            else { run=0; }
        }
        if (run<pages) return nullptr;
        // mark
        g_pageTags[start] = (uint8_t)tag; for(size_t k=1;k<pages;k++) g_pageTags[start+k] = 0xFF; // continuation
        g_tagPageCount[(size_t)tag] += pages;
        g_stat.pagesInUse += pages; g_stat.peakPages = std::max(g_stat.peakPages, g_stat.pagesInUse);
        // write header
        auto* hdr = (BlockHeader*)(g_heap + start*PageSize);
        hdr->pages = (uint32_t)pages; hdr->tag = tag; hdr->pid = t_pid;
        {
            std::lock_guard<std::mutex> lk(g_pidMu);
            g_pidPageCount[hdr->pid] += pages;
        }
        return (void*)(hdr+1);
    }

    void* Allocator::realloc(void* ptr, size_t bytes){
        if (!ptr) return alloc(bytes);
        if (bytes==0){ free(ptr); return nullptr; }
        auto* hdr = (BlockHeader*)ptr - 1; size_t oldPages = hdr->pages; auto tag = hdr->tag; uint64_t oldPid = hdr->pid;
        void* n = alloc(bytes, tag); if (!n) return nullptr;
        auto* nh = (BlockHeader*)n - 1; nh->pid = oldPid; // preserve ownership
        std::memcpy(n, ptr, std::min(bytes, oldPages*PageSize - sizeof(BlockHeader)));
        free(ptr);
        return n;
    }

    void Allocator::free(void* ptr){
        if (!ptr) return; std::lock_guard<std::mutex> _g(g_lock);
        auto* hdr = (BlockHeader*)ptr - 1; size_t pages = hdr->pages; size_t start = ((uint8_t*)hdr - g_heap) / PageSize; auto tag = hdr->tag; auto pid = hdr->pid;
        for (size_t k=0;k<pages;k++) g_pageTags[start+k]=0;
        g_tagPageCount[(size_t)tag] -= pages;
        g_stat.pagesInUse -= pages;
        g_stat.pagesFreed += pages;
        {
            std::lock_guard<std::mutex> lk(g_pidMu);
            auto it = g_pidPageCount.find(pid); if(it!=g_pidPageCount.end()){ if(it->second>pages) it->second-=pages; else it->second=0; }
        }
    }

    uint64_t Allocator::bytesInUse(){ std::lock_guard<std::mutex> _g(g_lock); return g_stat.pagesInUse*PageSize; }
    uint64_t Allocator::peakBytes(){ std::lock_guard<std::mutex> _g(g_lock); return g_stat.peakPages*PageSize; }
    uint64_t Allocator::totalSize(){ std::lock_guard<std::mutex> _g(g_lock); return g_pagesTotal*PageSize; }
    uint64_t Allocator::totalFreed(){ std::lock_guard<std::mutex> _g(g_lock); return g_stat.pagesFreed*PageSize; }
    uint64_t Allocator::tagBytes(AllocTag tag){ std::lock_guard<std::mutex> _g(g_lock); return g_tagPageCount[(size_t)tag]*PageSize; }

    void Allocator::setCurrentPid(uint64_t pid){ t_pid = pid; }
    uint64_t Allocator::currentPid(){ return t_pid; }
    uint64_t Allocator::pidBytes(uint64_t pid){ std::lock_guard<std::mutex> lk(g_pidMu); auto it=g_pidPageCount.find(pid); return it==g_pidPageCount.end()?0:it->second*PageSize; }
    std::vector<std::pair<uint64_t,uint64_t>> Allocator::listPidBytes(){ std::lock_guard<std::mutex> lk(g_pidMu); std::vector<std::pair<uint64_t,uint64_t>> v; v.reserve(g_pidPageCount.size()); for(auto& kv: g_pidPageCount) v.emplace_back(kv.first, kv.second*PageSize); return v; }
}
