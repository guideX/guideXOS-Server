#pragma once

#include <stdint.h>

static constexpr uint64_t kGuideXosR25FailFastOriginMagic = 0x4738523235464154ull;
static constexpr uint64_t kGuideXosR25FailFastOriginSchema = 1u;
static constexpr uint64_t kGuideXosR25FailFastOriginPublished = 0x5055424C49534844ull;
static constexpr uint32_t kGuideXosR25FailFastFastFailCode = 7u;
static constexpr uint32_t kGuideXosR25FailFastStageUnknown = 0u;
static constexpr uint32_t kGuideXosR25FailFastStageNativeModulePreparation = 1u;
static constexpr uint32_t kGuideXosR25FailFastStageBeforeInitializeModules = 2u;
static constexpr uint32_t kGuideXosR25FailFastStageInitializeModulesReturned = 3u;

// Ten fixed-width words make this record directly readable with a bounded QEMU
// HMP memory query. It owns no NativeAOT, managed, GC, or transition-frame data.
struct GuideXosR25FailFastOriginRecord {
    uint64_t magic;
    uint64_t schemaVersion;
    uint64_t recordBytes;
    uint64_t originalReason;
    uint64_t fastFailCode;
    uint64_t callerCaptureReturnAddress;
    uint64_t failFastImplementationAddress;
    uint64_t initializationStage;
    uint64_t flags;
    uint64_t published;
};

static_assert(sizeof(GuideXosR25FailFastOriginRecord) == 80u,
    "R25 fail-fast record must remain a bounded ten-word payload");
