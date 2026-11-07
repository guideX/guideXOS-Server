#pragma once

#include <stdint.h>
#include "guidexos_nativeaot_thread_attachment_contract.h"

static constexpr uint32_t kGuideXosTransitionFatalMagic = 0x43464154u;
static constexpr uint32_t kGuideXosTransitionFatalSchema = 1u;
static constexpr uint32_t kGuideXosTransitionFatalPublished = 0x50424C53u;
static constexpr uint32_t kGuideXosTransitionFatalNoStage = 0xFFFFFFFFu;

enum GuideXosTransitionGateFailure : uint32_t {
    GX_TRANSITION_GATE_NONE = 0u,
    GX_TRANSITION_GATE_TLS = 1u,
    GX_TRANSITION_GATE_RHINITIALIZE = 2u,
    GX_TRANSITION_GATE_RUNTIME_INSTANCE = 3u,
    GX_TRANSITION_GATE_THREADSTORE = 4u,
    GX_TRANSITION_GATE_NATIVE_THREAD_ID = 5u,
    GX_TRANSITION_GATE_PAL_FLS = 6u,
    GX_TRANSITION_GATE_STACK_BOUNDS = 7u,
    GX_TRANSITION_GATE_GC_HEAP = 8u,
    GX_TRANSITION_GATE_MODULE_REGISTRATION = 9u,
    GX_TRANSITION_GATE_CURRENT_THREAD_STATE = 10u,
    GX_TRANSITION_GATE_TRANSITION_FRAME = 11u,
};

enum GuideXosTransitionSnapshotStatus : uint32_t {
    GX_TRANSITION_SNAPSHOT_NOT_CAPTURED = 0u,
    GX_TRANSITION_SNAPSHOT_READY = 1u,
    GX_TRANSITION_SNAPSHOT_BLOCKED = 2u,
};

static constexpr uint32_t kGuideXosTransitionCallSiteUnresolved = 0xFFFFFFFEu;

struct GuideXosTransitionCallSiteEntry {
    uintptr_t callerReturnAddress;
    uint32_t callSiteId;
};

static inline uint32_t GuideXosTransitionResolveCallSite(
    uintptr_t callerReturnAddress,
    const GuideXosTransitionCallSiteEntry* entries,
    uint32_t entryCount) {
    if (entries == nullptr) return kGuideXosTransitionCallSiteUnresolved;
    for (uint32_t index = 0u; index < entryCount; ++index) {
        if (callerReturnAddress != 0u &&
            entries[index].callerReturnAddress == callerReturnAddress)
            return entries[index].callSiteId;
    }
    return kGuideXosTransitionCallSiteUnresolved;
}

enum GuideXosTransitionMessageCapture : uint32_t {
    GX_TRANSITION_MESSAGE_UNAVAILABLE = 0u,
    GX_TRANSITION_MESSAGE_BOUNDED = 1u,
    GX_TRANSITION_MESSAGE_TRUNCATED = 2u,
};

enum GuideXosTransitionCooperativeState : uint32_t {
    GX_TRANSITION_COOP_UNKNOWN = 0u,
    GX_TRANSITION_COOP_PREEMPTIVE = 1u,
    GX_TRANSITION_COOP_COOPERATIVE = 2u,
    GX_TRANSITION_COOP_STOCK_ATTACH_WILL_INITIALIZE = 3u,
};

struct GuideXosNativeAotTransitionAttributionRecord {
    uint32_t magic;
    uint32_t schemaVersion;
    uint32_t recordBytes;
    uint32_t integrity;
    uint32_t snapshotSequence;
    uint32_t snapshotStatus;
    uint32_t transitionAttempted;
    uint32_t transitionCompleted;
    uint32_t transitionGateFailure;
    uint32_t fatalPublished;
    uint32_t fatalReason;
    uint32_t originalStatusAvailable;
    uint32_t originalStatus;
    uint32_t messageCaptureStatus;
    uint32_t messageBytes;
    uint32_t messageTruncated;
    uint32_t sourceCallSiteId;
    uint32_t firstFailedStage;
    uint32_t firstFailureStatusAvailable;
    uint32_t firstFailureStatus;
    uint32_t lastSuccessfulStage;
    uint32_t initTraceOverflow;
    uint32_t threadReadinessOverflow;
    uint32_t runtimeInstanceReady;
    uint32_t rhInitializeReady;
    uint32_t runtimeStartupReady;
    uint32_t runtimeFlsCellReady;
    uint32_t modulesNotInitialized;
    uint32_t threadStoreReady;
    uint32_t tlsReady;
    uint32_t flsIndexReady;
    uint32_t flsCurrentThreadState;
    uint32_t currentThreadAttachment;
    uint32_t threadStateFlags;
    uint32_t cooperativeState;
    uint32_t palStackBoundsReady;
    uint32_t currentPointerInStackBounds;
    uint32_t transitionFrameReady;
    uint32_t transitionFrameAligned;
    uint32_t transitionFrameInStackBounds;
    uint32_t transitionFrameWillBeCreatedByStock;
    uint32_t gcHeapReady;
    uint32_t osModuleRegistered;
    uint32_t codeManagerRegistered;
    uint32_t messageIdResolvedByCallerRip;
    uintptr_t callerReturnAddress;
    uintptr_t messagePointer;
    uintptr_t runtimeInstance;
    uintptr_t threadStore;
    uintptr_t rawCurrentThread;
    uintptr_t currentThread;
    uintptr_t tlsVector;
    uintptr_t tlsBlock;
    uintptr_t tlsIndex;
    uintptr_t palFlsIndex;
    uintptr_t nativeThreadId;
    uintptr_t observedOsThreadId;
    uintptr_t flsCurrentThread;
    uintptr_t stackLow;
    uintptr_t stackHigh;
    uintptr_t stackCurrent;
    uintptr_t transitionFrame;
};

static inline uint32_t GuideXosTransitionEvaluateGate(
    const GuideXosNativeAotTransitionAttributionRecord* record) {
    if (record == nullptr || record->tlsReady == 0u ||
        record->runtimeStartupReady == 0u || record->runtimeFlsCellReady == 0u)
        return GX_TRANSITION_GATE_TLS;
    if (record->rhInitializeReady == 0u)
        return GX_TRANSITION_GATE_RHINITIALIZE;
    if (record->runtimeInstanceReady == 0u)
        return GX_TRANSITION_GATE_RUNTIME_INSTANCE;
    if (record->threadStoreReady == 0u || record->rawCurrentThread == 0u)
        return GX_TRANSITION_GATE_THREADSTORE;
    if (record->observedOsThreadId == 0u)
        return GX_TRANSITION_GATE_NATIVE_THREAD_ID;
    if (record->flsIndexReady == 0u ||
        (record->currentThreadAttachment == GX_TRANSITION_ATTACHMENT_PRESENT &&
         record->flsCurrentThreadState != GX_TRANSITION_PAL_FLS_MATCHES_CURRENT_THREAD) ||
        (record->currentThreadAttachment == GX_TRANSITION_ATTACHMENT_ABSENT &&
         record->flsCurrentThreadState != GX_TRANSITION_PAL_FLS_EMPTY) ||
        record->currentThreadAttachment == GX_TRANSITION_ATTACHMENT_UNKNOWN)
        return GX_TRANSITION_GATE_PAL_FLS;
    if (record->palStackBoundsReady == 0u ||
        record->currentPointerInStackBounds == 0u)
        return GX_TRANSITION_GATE_STACK_BOUNDS;
    if (record->gcHeapReady == 0u)
        return GX_TRANSITION_GATE_GC_HEAP;
    if (record->osModuleRegistered == 0u || record->codeManagerRegistered == 0u ||
        record->modulesNotInitialized == 0u)
        return GX_TRANSITION_GATE_MODULE_REGISTRATION;
    if (record->currentThreadAttachment == GX_TRANSITION_ATTACHMENT_ABSENT) {
        // ReversePInvokeAttachOrTrapThread only constructs the thread when
        // IsInitialized() is false; any nonzero state without TSF_Attached is unsafe.
        if (record->threadStateFlags != 0u ||
            record->transitionFrameWillBeCreatedByStock == 0u ||
            record->cooperativeState != GX_TRANSITION_COOP_STOCK_ATTACH_WILL_INITIALIZE)
            return GX_TRANSITION_GATE_CURRENT_THREAD_STATE;
        return GX_TRANSITION_GATE_NONE;
    }
    const uintptr_t topOfStackMarker = ~static_cast<uintptr_t>(0);
    const bool validTransitionFrame = record->transitionFrame == topOfStackMarker ||
        (record->transitionFrameInStackBounds != 0u &&
         record->transitionFrameAligned != 0u);
    if ((record->threadStateFlags & 0x1u) == 0u ||
        record->cooperativeState != GX_TRANSITION_COOP_PREEMPTIVE ||
        record->transitionFrameReady == 0u || !validTransitionFrame ||
        record->nativeThreadId != record->observedOsThreadId)
        return GX_TRANSITION_GATE_CURRENT_THREAD_STATE;
    return GX_TRANSITION_GATE_NONE;
}

static inline uint32_t GuideXosTransitionAttributionIntegrity(
    const GuideXosNativeAotTransitionAttributionRecord* record) {
    if (record == nullptr) return 0u;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(record);
    uint32_t value = 0xC188A7E3u;
    for (uint32_t index = 16u;
         index < sizeof(GuideXosNativeAotTransitionAttributionRecord); ++index) {
        value = (value << 5u) | (value >> 27u);
        value ^= static_cast<uint32_t>(bytes[index]) + (0x9E3779B9u ^ index);
    }
    return value;
}

static inline bool GuideXosTransitionAttributionRecordValid(
    const GuideXosNativeAotTransitionAttributionRecord* record) {
    return record != nullptr &&
        record->magic == kGuideXosTransitionFatalMagic &&
        record->schemaVersion == kGuideXosTransitionFatalSchema &&
        record->recordBytes == sizeof(GuideXosNativeAotTransitionAttributionRecord) &&
        record->integrity == GuideXosTransitionAttributionIntegrity(record);
}

static inline void GuideXosTransitionAttributionSeal(
    GuideXosNativeAotTransitionAttributionRecord* record) {
    if (record == nullptr) return;
    record->magic = kGuideXosTransitionFatalMagic;
    record->schemaVersion = kGuideXosTransitionFatalSchema;
    record->recordBytes = static_cast<uint32_t>(
        sizeof(GuideXosNativeAotTransitionAttributionRecord));
    record->integrity = 0u;
    record->integrity = GuideXosTransitionAttributionIntegrity(record);
}
