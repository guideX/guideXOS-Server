#pragma once

#include <stdint.h>

namespace guideXOS {
namespace amd64_boot_paging {

// Four-level boot tree shared by the loader and the kernel. The ceiling
// covers the root, all intermediate and leaf tables, and self-mapping.
constexpr uint32_t kMaxTablePages = 1024u;
constexpr uint64_t kIdentityLimit = 1ull << 47;
constexpr uint64_t kPageBytes = 4096u;
constexpr uint64_t kPageMask = kPageBytes - 1u;
constexpr uint64_t kAddressMask = 0x000FFFFFFFFFF000ull;

}
}
