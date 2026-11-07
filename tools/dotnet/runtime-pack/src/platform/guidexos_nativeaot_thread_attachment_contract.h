#pragma once

#include <stdint.h>

enum GuideXosTransitionAttachmentState : uint32_t {
    GX_TRANSITION_ATTACHMENT_UNKNOWN = 0u,
    GX_TRANSITION_ATTACHMENT_ABSENT = 1u,
    GX_TRANSITION_ATTACHMENT_PRESENT = 2u,
};

enum GuideXosTransitionPalFlsState : uint32_t {
    GX_TRANSITION_PAL_FLS_EMPTY = 0u,
    GX_TRANSITION_PAL_FLS_MATCHES_CURRENT_THREAD = 1u,
    GX_TRANSITION_PAL_FLS_FOREIGN_THREAD = 2u,
    GX_TRANSITION_PAL_FLS_UNAVAILABLE = 3u,
};

static inline GuideXosTransitionAttachmentState
GuideXosTransitionClassifyAttachment(uint32_t threadStateFlags) {
    return (threadStateFlags & 0x1u) != 0u
        ? GX_TRANSITION_ATTACHMENT_PRESENT
        : GX_TRANSITION_ATTACHMENT_ABSENT;
}

static inline GuideXosTransitionPalFlsState GuideXosTransitionClassifyPalFls(
    bool flsIndexReady,
    uintptr_t flsValue,
    uintptr_t rawCurrentThread) {
    if (!flsIndexReady) return GX_TRANSITION_PAL_FLS_UNAVAILABLE;
    if (flsValue == 0u) return GX_TRANSITION_PAL_FLS_EMPTY;
    if (rawCurrentThread != 0u && flsValue == rawCurrentThread)
        return GX_TRANSITION_PAL_FLS_MATCHES_CURRENT_THREAD;
    return GX_TRANSITION_PAL_FLS_FOREIGN_THREAD;
}

// Idempotent non-managed sentinel initialization. The only permitted write is
// to the tls_CurrentThread pointer cell itself; neighboring Thread fields are
// never used as marker storage.
static inline bool GuideXosTransitionInitializeLegacyRuntimeCell(void** cell) {
    if (cell == nullptr || *cell != nullptr) return false;
    *cell = cell;
    return true;
}
