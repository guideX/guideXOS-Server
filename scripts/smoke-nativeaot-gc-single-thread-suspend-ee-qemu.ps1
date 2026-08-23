param(
    [string]$RepoRoot = "",
    [string]$EvidenceRoot = "",
    [int]$TimeoutSeconds = 90,
    [int]$FreshBootCount = 3,
    [switch]$SkipManagedBuild,
    [ValidateSet("single-thread-suspend-ee", "allocation-context-fixup-root-boundary", "first-per-thread-root-provider", "first-root-candidate-load", "first-non-null-root-callback-boundary", "first-root-callback-entry", "first-root-membership-classification", "first-root-heap-resolution", "first-root-condemned-generation-decision", "first-root-pre-mark-boundary", "first-root-first-mark-mutation", "first-root-post-queue-mark-decision", "first-root-first-non-null-old-o", "next-genuine-root-provider", "stack-provider-transition-failfast", "stack-provider-code-manager-registration", "stack-provider-transition-frame-control-pc", "stack-provider-unwind-gc-info", "stack-provider-unwind-caller-frame", "stack-provider-native-transition-continuation", "stack-provider-native-caller-provenance", "stack-provider-native-kernel-entry-boundary", "stack-provider-native-kernel-stack-completion", "post-root-queue-mark-processing", "mark-queue-closure", "post-mark-short-weak-handle", "short-weak-handle-operation", "short-weak-live-handle", "short-weak-dead-handle", "short-weak-lifetime-transition", "relocation-root-update")]
    [string]$ProofMode = "single-thread-suspend-ee"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
if ($FreshBootCount -lt 1) { throw "FreshBootCount must be at least 1." }

function Replace-First([string]$text, [string]$old, [string]$new) {
    $offset = $text.IndexOf($old, [System.StringComparison]::Ordinal)
    if ($offset -lt 0) { throw "Requested source replacement was not found." }
    return $text.Substring(0, $offset) + $new + $text.Substring($offset + $old.Length)
}

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}
$root = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = if ($ProofMode -eq "first-root-first-non-null-old-o") {
        Join-Path $root "out\dotnet\gc-first-root-first-non-null-old-o"
    } elseif ($ProofMode -eq "next-genuine-root-provider") {
        Join-Path $root "out\dotnet\gc-next-genuine-root-provider"
    } elseif ($ProofMode -eq "stack-provider-transition-failfast") {
        Join-Path $root "out\dotnet\gc-stack-provider-transition-failfast"
    } elseif ($ProofMode -eq "stack-provider-code-manager-registration") {
        Join-Path $root "out\dotnet\gc-stack-provider-code-manager-registration"
    } elseif ($ProofMode -eq "stack-provider-transition-frame-control-pc") {
        Join-Path $root "out\dotnet\gc-stack-provider-transition-frame-control-pc"
    } elseif ($ProofMode -eq "stack-provider-unwind-gc-info") {
        Join-Path $root "out\dotnet\gc-stack-provider-unwind-gc-info"
    } elseif ($ProofMode -eq "stack-provider-unwind-caller-frame") {
        Join-Path $root "out\dotnet\gc-stack-provider-unwind-caller-frame"
    } elseif ($ProofMode -eq "stack-provider-native-transition-continuation") {
        Join-Path $root "out\dotnet\gc-stack-provider-native-transition-continuation"
    } elseif ($ProofMode -eq "stack-provider-native-caller-provenance") {
        Join-Path $root "out\dotnet\c011ec24-native-caller-provenance"
    } elseif ($ProofMode -eq "stack-provider-native-kernel-entry-boundary") {
        Join-Path $root "out\dotnet\c011ec25-kernel-entry-boundary"
    } elseif ($ProofMode -eq "stack-provider-native-kernel-stack-completion") {
        Join-Path $root "out\dotnet\c011ec26-stack-walk-completion"
    } elseif ($ProofMode -eq "post-root-queue-mark-processing") {
        Join-Path $root "out\dotnet\c011ec27-post-root-queue-mark-processing"
    } elseif ($ProofMode -eq "mark-queue-closure") {
        Join-Path $root "out\dotnet\c011ec28-mark-queue-closure"
    } elseif ($ProofMode -eq "short-weak-handle-operation") {
        Join-Path $root "out\dotnet\c011ec30-short-weak-handle-operation"
    } elseif ($ProofMode -eq "short-weak-live-handle") {
        Join-Path $root "out\dotnet\c011ec31-short-weak-live-handle"
    } elseif ($ProofMode -eq "short-weak-dead-handle") {
        Join-Path $root "out\dotnet\c011ec32-short-weak-dead-handle"
    } elseif ($ProofMode -eq "short-weak-lifetime-transition") {
        Join-Path $root "out\dotnet\c011ec33-short-weak-lifetime-transition"
    } elseif ($ProofMode -eq "relocation-root-update") {
        Join-Path $root "out\dotnet\c011ec34-relocation-root-update"
    } elseif ($ProofMode -eq "post-mark-short-weak-handle") {
        Join-Path $root "out\dotnet\c011ec29-post-mark-short-weak-handle"
    } elseif ($ProofMode -eq "first-root-post-queue-mark-decision") {
        Join-Path $root "out\dotnet\gc-first-root-post-queue-mark-decision"
    } elseif ($ProofMode -eq "first-root-first-mark-mutation") {
        Join-Path $root "out\dotnet\gc-first-root-first-mark-mutation"
    } elseif ($ProofMode -eq "first-root-pre-mark-boundary") {
        Join-Path $root "out\dotnet\gc-first-root-pre-mark-boundary"
    } elseif ($ProofMode -eq "first-root-condemned-generation-decision") {
        Join-Path $root "out\dotnet\gc-first-root-condemned-generation-decision"
    } elseif ($ProofMode -eq "first-root-heap-resolution") {
        Join-Path $root "out\dotnet\gc-first-root-heap-resolution"
    } elseif ($ProofMode -eq "first-root-membership-classification") {
        Join-Path $root "out\dotnet\gc-first-root-membership-classification"
    } elseif ($ProofMode -eq "first-root-callback-entry") {
        Join-Path $root "out\dotnet\gc-first-root-callback-entry"
    } elseif ($ProofMode -eq "first-non-null-root-callback-boundary") {
        Join-Path $root "out\dotnet\gc-first-non-null-root-callback-boundary"
    } elseif ($ProofMode -eq "first-root-candidate-load") {
        Join-Path $root "out\dotnet\gc-first-root-candidate-load"
    } elseif ($ProofMode -eq "first-per-thread-root-provider") {
        Join-Path $root "out\dotnet\gc-first-per-thread-root-provider"
    } elseif ($ProofMode -eq "allocation-context-fixup-root-boundary") {
        Join-Path $root "out\dotnet\gc-allocation-context-fixup-root-boundary"
    } else {
        Join-Path $root "out\dotnet\gc-single-thread-suspend-ee"
    }
}
$isFirstRootCandidateLoad = $ProofMode -eq "first-root-candidate-load"
$isFirstRootHeapResolution = $ProofMode -eq "first-root-heap-resolution"
$isFirstRootCondemnedGenerationDecision = $ProofMode -eq "first-root-condemned-generation-decision"
$isFirstRootFirstMarkMutation = $ProofMode -eq "first-root-first-mark-mutation"
$isFirstRootPostQueueMarkDecision = $ProofMode -eq "first-root-post-queue-mark-decision"
$isFirstRootFirstNonNullOldO = $ProofMode -eq "first-root-first-non-null-old-o"
$isStackProviderTransitionFailFast = $ProofMode -eq "stack-provider-transition-failfast"
$isCodeManagerRegistration = $ProofMode -eq "stack-provider-code-manager-registration"
$isTransitionFrameControlPc = $ProofMode -eq "stack-provider-transition-frame-control-pc"
$isC011EC34 = $ProofMode -eq "relocation-root-update"
$isC011EC33 = $ProofMode -in @("short-weak-lifetime-transition", "relocation-root-update")
$isC011EC31 = $ProofMode -eq "short-weak-live-handle"
$isC011EC32 = $ProofMode -in @("short-weak-dead-handle", "short-weak-lifetime-transition", "relocation-root-update")
$isC011EC30 = $isC011EC31 -or $isC011EC32 -or ($ProofMode -eq "short-weak-handle-operation")
$isC011EC29 = $ProofMode -in @("post-mark-short-weak-handle", "short-weak-handle-operation", "short-weak-live-handle", "short-weak-dead-handle", "short-weak-lifetime-transition", "relocation-root-update")
$isC011EC28 = $isC011EC29 -or ($ProofMode -eq "mark-queue-closure")
$isC011EC27Stop = $ProofMode -eq "post-root-queue-mark-processing"
$isC011EC27 = $isC011EC28 -or $isC011EC27Stop
$isC011EC26 = $isC011EC27 -or $ProofMode -eq "stack-provider-native-kernel-stack-completion"
$isC011EC25 = $isC011EC26 -or $ProofMode -eq "stack-provider-native-kernel-entry-boundary"
$isC011EC24 = $isC011EC25 -or $ProofMode -eq "stack-provider-native-caller-provenance"
$isC011EC21 = $isC011EC24 -or $ProofMode -eq "stack-provider-native-transition-continuation"
$isC011EC23 = $isC011EC21
$isC011EC20 = $isC011EC24 -or $ProofMode -in @("stack-provider-unwind-caller-frame", "stack-provider-native-transition-continuation")
$isC011EC19 = $isC011EC24 -or $ProofMode -in @("stack-provider-unwind-gc-info", "stack-provider-unwind-caller-frame", "stack-provider-native-transition-continuation")
$isNextGenuineRootProvider = $isC011EC24 -or $ProofMode -in @("next-genuine-root-provider", "stack-provider-transition-failfast", "stack-provider-code-manager-registration", "stack-provider-transition-frame-control-pc", "stack-provider-unwind-gc-info", "stack-provider-unwind-caller-frame", "stack-provider-native-transition-continuation", "mark-queue-closure", "post-mark-short-weak-handle", "short-weak-handle-operation", "short-weak-live-handle", "short-weak-dead-handle", "short-weak-lifetime-transition", "relocation-root-update")
$isFirstRootPreMarkBoundary = $ProofMode -in @("first-root-pre-mark-boundary", "first-root-first-mark-mutation", "first-root-post-queue-mark-decision")
$isFirstRootHeapResolutionOrCondemned = $isFirstRootHeapResolution -or $isFirstRootCondemnedGenerationDecision -or $isFirstRootPreMarkBoundary
$isFirstRootCondemnedGenerationDecisionOrPreMark = $isFirstRootCondemnedGenerationDecision -or $isFirstRootPreMarkBoundary
$isFirstRootMembershipClassification = $ProofMode -eq "first-root-membership-classification"
$isFirstRootCallbackEntry = $isC011EC24 -or $ProofMode -in @("first-root-callback-entry", "first-root-membership-classification", "first-root-heap-resolution", "first-root-condemned-generation-decision", "first-root-pre-mark-boundary", "first-root-first-mark-mutation", "first-root-post-queue-mark-decision", "first-root-first-non-null-old-o", "next-genuine-root-provider", "stack-provider-transition-failfast", "stack-provider-code-manager-registration", "stack-provider-transition-frame-control-pc", "stack-provider-unwind-gc-info", "stack-provider-unwind-caller-frame", "stack-provider-native-transition-continuation", "mark-queue-closure", "post-mark-short-weak-handle", "short-weak-handle-operation", "short-weak-live-handle", "short-weak-dead-handle", "relocation-root-update")
$isFirstNonNullRoot = $ProofMode -eq "first-non-null-root-callback-boundary"
$isCandidateLoadEnumeration = $isFirstRootCandidateLoad -or $isFirstNonNullRoot -or $isFirstRootCallbackEntry
$isFirstPerThreadRootProvider = $isC011EC24 -or $ProofMode -in @("first-per-thread-root-provider", "first-root-candidate-load", "first-non-null-root-callback-boundary", "first-root-callback-entry", "first-root-membership-classification", "first-root-heap-resolution", "first-root-condemned-generation-decision", "first-root-pre-mark-boundary", "first-root-first-mark-mutation", "first-root-post-queue-mark-decision", "first-root-first-non-null-old-o", "next-genuine-root-provider", "stack-provider-transition-failfast", "stack-provider-code-manager-registration", "stack-provider-transition-frame-control-pc", "stack-provider-unwind-gc-info", "stack-provider-unwind-caller-frame", "stack-provider-native-transition-continuation", "short-weak-handle-operation", "short-weak-live-handle", "short-weak-dead-handle", "relocation-root-update")
$isAllocationContextFixupRootBoundary = $isC011EC24 -or $ProofMode -in @("allocation-context-fixup-root-boundary", "first-per-thread-root-provider", "first-root-candidate-load", "first-non-null-root-callback-boundary", "first-root-callback-entry", "first-root-membership-classification", "first-root-heap-resolution", "first-root-condemned-generation-decision", "first-root-pre-mark-boundary", "first-root-first-mark-mutation", "first-root-post-queue-mark-decision", "first-root-first-non-null-old-o", "next-genuine-root-provider", "stack-provider-transition-failfast", "stack-provider-code-manager-registration", "stack-provider-transition-frame-control-pc", "stack-provider-unwind-gc-info", "stack-provider-unwind-caller-frame", "stack-provider-native-transition-continuation", "short-weak-handle-operation", "short-weak-live-handle", "short-weak-dead-handle", "relocation-root-update")
$proofDefine = if ($isNextGenuineRootProvider) {
    $minimalDefine = if ($isStackProviderTransitionFailFast) { " /DGUIDEXOS_NATIVEAOT_STACK_PROVIDER_TRANSITION_FAILFAST_MINIMAL" } else { "" }
    $codeManagerDefine = if ($isCodeManagerRegistration) { " /DGUIDEXOS_NATIVEAOT_C011EC17_CODE_MANAGER" } elseif ($isTransitionFrameControlPc -or $isC011EC19) { " /DGUIDEXOS_NATIVEAOT_USE_STOCK_RHP_NEW_ARRAY_ENTRY /DGUIDEXOS_NATIVEAOT_C011EC18_NATIVE_RHP_NEW_ARRAY" } else { "" }
    $c19Define = if ($isC011EC19) { " /DGUIDEXOS_NATIVEAOT_C011EC19_UNWIND_GC_INFO" } else { "" }
    $c20Define = if ($isC011EC20) { " /DGUIDEXOS_NATIVEAOT_C011EC20_UNWIND" } else { "" }
    $c21Define = if ($isC011EC21) { " /DGUIDEXOS_NATIVEAOT_C011EC21_NATIVE_CONTINUATION" } else { "" }
    $c23Define = if ($isC011EC23) { " /DGUIDEXOS_NATIVEAOT_C011EC23_NATIVE_UNWIND" } else { "" }
    $c24Define = if ($isC011EC24) { " /DGUIDEXOS_NATIVEAOT_C011EC24_CALLER_PROVENANCE" } else { "" }
    $c25Define = if ($isC011EC25) { " /DGUIDEXOS_NATIVEAOT_C011EC25_KERNEL_ENTRY_BOUNDARY" } else { "" }
    $c26Define = if ($isC011EC26) { " /DGUIDEXOS_NATIVEAOT_C011EC26_STACK_COMPLETION" } else { "" }
    $c27Define = if ($isC011EC27) { " /DGUIDEXOS_NATIVEAOT_C011EC27_POST_ROOT_QUEUE" } else { "" }
    $c28Define = if ($isC011EC28) { " /DGUIDEXOS_NATIVEAOT_C011EC28_MARK_QUEUE_CLOSURE" } else { "" }
    $c29Define = if ($isC011EC29) { " /DGUIDEXOS_NATIVEAOT_C011EC29_POST_MARK_PHASE" } else { "" }
    $c31Define = if ($isC011EC31) { " /DGUIDEXOS_NATIVEAOT_C011EC31_LIVE_SHORT_WEAK" } else { "" }
    $c32Define = if ($isC011EC32) { " /DGUIDEXOS_NATIVEAOT_C011EC32_DEAD_SHORT_WEAK" } else { "" }
    $c33Define = if ($isC011EC33) { " /DGUIDEXOS_NATIVEAOT_C011EC33_LIFETIME_TRANSITION" } else { "" }
    $c34Define = if ($isC011EC34) { " /DGUIDEXOS_NATIVEAOT_C011EC34_RELOCATION_ROOT_UPDATE" } else { "" }
    $firstNonNullDefine = if ($isC011EC31 -or $isC011EC32) { "" } else { " /DGUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_ALLOCATION" }
    "/DGUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_PER_THREAD_ROOT_PROVIDER_ALLOCATION$firstNonNullDefine /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CALLBACK_ENTRY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_HEAP_RESOLUTION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CONDEMNED_GENERATION_DECISION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_PRE_MARK_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_NON_NULL_OLD_O_ALLOCATION /DGUIDEXOS_NATIVEAOT_NEXT_GENUINE_ROOT_PROVIDER_ALLOCATION$minimalDefine$codeManagerDefine$c19Define$c20Define$c21Define$c23Define$c24Define$c25Define$c26Define$c27Define$c28Define$c29Define$c31Define$c32Define$c33Define$c34Define"
} elseif ($isFirstRootFirstNonNullOldO) {
    "/DGUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_PER_THREAD_ROOT_PROVIDER_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CALLBACK_ENTRY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_HEAP_RESOLUTION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CONDEMNED_GENERATION_DECISION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_PRE_MARK_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_NON_NULL_OLD_O_ALLOCATION"
} elseif ($isFirstRootPostQueueMarkDecision) {
    "/DGUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_PER_THREAD_ROOT_PROVIDER_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CALLBACK_ENTRY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_HEAP_RESOLUTION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CONDEMNED_GENERATION_DECISION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_PRE_MARK_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_FIRST_MARK_MUTATION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_POST_QUEUE_MARK_DECISION_ALLOCATION"
} elseif ($isFirstRootFirstMarkMutation) {
    "/DGUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_PER_THREAD_ROOT_PROVIDER_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CALLBACK_ENTRY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_HEAP_RESOLUTION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CONDEMNED_GENERATION_DECISION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_PRE_MARK_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_FIRST_MARK_MUTATION_ALLOCATION"
} elseif ($isFirstRootPreMarkBoundary) {
    "/DGUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_PER_THREAD_ROOT_PROVIDER_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CALLBACK_ENTRY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_HEAP_RESOLUTION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CONDEMNED_GENERATION_DECISION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_PRE_MARK_BOUNDARY_ALLOCATION"
} elseif ($isFirstRootCondemnedGenerationDecision) {
    "/DGUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_PER_THREAD_ROOT_PROVIDER_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CALLBACK_ENTRY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_HEAP_RESOLUTION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CONDEMNED_GENERATION_DECISION_ALLOCATION"
} elseif ($isFirstRootHeapResolution) {
    "/DGUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_PER_THREAD_ROOT_PROVIDER_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CALLBACK_ENTRY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_HEAP_RESOLUTION_ALLOCATION"
} elseif ($isFirstRootMembershipClassification) {
    "/DGUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_PER_THREAD_ROOT_PROVIDER_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CALLBACK_ENTRY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION_ALLOCATION"
} elseif ($isFirstRootCallbackEntry) {
    "/DGUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_PER_THREAD_ROOT_PROVIDER_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CALLBACK_ENTRY_ALLOCATION"
} elseif ($isFirstNonNullRoot) {
    "/DGUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_PER_THREAD_ROOT_PROVIDER_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_ALLOCATION"
} elseif ($isFirstRootCandidateLoad) {
    "/DGUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_PER_THREAD_ROOT_PROVIDER_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CANDIDATE_LOAD_ALLOCATION"
} elseif ($isFirstPerThreadRootProvider) {
    "/DGUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_PER_THREAD_ROOT_PROVIDER_ALLOCATION"
} elseif ($isAllocationContextFixupRootBoundary) {
    "/DGUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION"
} else {
    ""
}
$evidence = [System.IO.Path]::GetFullPath($EvidenceRoot)
$runRoot = Join-Path $evidence (Get-Date -Format "run-yyyyMMdd-HHmmssfff")
$buildRoot = Join-Path $evidence ("build-" + (Split-Path -Leaf $runRoot))
$artifactRoot = Join-Path $buildRoot "artifact"
$runtimeRoot = Join-Path $buildRoot "runtime-pack"
$oldRoot = Join-Path $root "out\dotnet\gc-first-real-allocation"
$oldArtifact = Join-Path $oldRoot "managed-artifact"
$gcStartupRoot = Join-Path $root "out\dotnet\gc-initialization-dry-run\artifact"
$managedPublishRoot = Join-Path $buildRoot "managed"

$pePath = Join-Path $artifactRoot "NativeAotGcSingleThreadSuspendEe.exe"
$elfPath = Join-Path $artifactRoot "NativeAotGcSingleThreadSuspendEe.elf"
$mapPath = Join-Path $artifactRoot "NativeAotGcSingleThreadSuspendEe.map"
$kernelPath = Join-Path $root "kernel\build\amd64\bin\kernel.elf"
$espKernelPath = Join-Path $root "ESP\kernel.elf"
$normalKernelSource = Join-Path $buildRoot "ordinary-kernel-before.elf"
$normalKernelHash = "75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6"
$historicalNormalKernelHash = $normalKernelHash
$activeArchive = Join-Path $root "out\dotnet\pal-runtime-active-replacement\archives\Runtime.WorkstationGC.guidexos-nativeaot-pal.lib"
$activeArchiveHash = "C617D95647A20862947B52A1301DF96FE9104E5A13F11BB0C016B8370DDE115F"
$bootloader = Join-Path $root "guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe"
$qemu = "C:\Program Files\qemu\qemu-system-x86_64.exe"
$ovmf = "C:\Program Files\qemu\share\edk2-x86_64-code.fd"
$objcopy = "C:\mingw64\bin\objcopy.exe"
$objdump = "C:\mingw64\bin\objdump.exe"
$readelf = "C:\mingw64\bin\readelf.exe"
$python = Join-Path $env:USERPROFILE ".cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
$converter = Join-Path $root "tools\dotnet\pe_to_elf_v2_fixed_base.py"
$vsBat = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
$dotnet = "C:\Program Files\dotnet\dotnet.exe"
$make = (Get-Command mingw32-make.exe -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if ([string]::IsNullOrWhiteSpace($make)) { $make = (Get-Command make.exe -ErrorAction SilentlyContinue | Select-Object -First 1).Source }

function Require-File([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "$Label is missing: $Path" }
}

function Hash-File([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Log-Command([string]$Text) {
    Add-Content -LiteralPath (Join-Path $runRoot "commands.txt") -Value $Text
}

function Invoke-LoggedCommand {
    param([string]$FilePath, [string[]]$Arguments, [string]$LogPath, [string]$WorkingDirectory = $root)
    $commandText = '"' + $FilePath + '" ' + (($Arguments | ForEach-Object {
        if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ }
    }) -join ' ')
    Log-Command $commandText
    $oldPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        Push-Location -LiteralPath $WorkingDirectory
        try { & $FilePath @Arguments *> $LogPath; $exitCode = $LASTEXITCODE } finally { Pop-Location }
    } finally { $ErrorActionPreference = $oldPreference }
    if ($exitCode -ne 0) { throw "Command failed with exit code ${exitCode}: $commandText" }
}

function Write-Batch([string]$Name, [string]$Text) {
    $path = Join-Path $buildRoot $Name
    Set-Content -LiteralPath $path -Value $Text -Encoding ASCII
    return $path
}

function Invoke-Batch([string]$Path, [string]$LogName) {
    Invoke-LoggedCommand "cmd.exe" @("/d", "/c", "call `"$Path`"") (Join-Path $runRoot $LogName)
}

function Assert-Text([string]$Text, [string]$Pattern, [string]$Label) {
    if ($Text -notmatch $Pattern) { throw "Missing NativeAOT GC evidence for ${Label}: ${Pattern}" }
}

function Extract-MapAddress([string]$MapText, [string]$Symbol) {
    $pattern = '(?m)^\s+\S+\s+' + [regex]::Escape($Symbol) + '\s+([0-9A-Fa-f]{16})(?:\s+f)?\s'
    $match = [regex]::Match($MapText, $pattern)
    if (-not $match.Success) { throw "Map is missing export $Symbol" }
    return $match.Groups[1].Value
}

function Read-Monitor([int]$Port, [string]$Path) {
    try {
        $client = [System.Net.Sockets.TcpClient]::new()
        $client.Connect("127.0.0.1", $Port)
        $stream = $client.GetStream()
        $bytes = [Text.Encoding]::ASCII.GetBytes("info registers`r`ninfo cpus`r`ninfo mtree`r`n")
        $stream.Write($bytes, 0, $bytes.Length)
        Start-Sleep -Milliseconds 500
        $buffer = New-Object byte[] 65536
        $builder = [Text.StringBuilder]::new()
        while ($stream.DataAvailable) {
            $count = $stream.Read($buffer, 0, $buffer.Length)
            if ($count -le 0) { break }
            [void]$builder.Append([Text.Encoding]::ASCII.GetString($buffer, 0, $count))
        }
        Set-Content -LiteralPath $Path -Value $builder.ToString() -Encoding ASCII
        $client.Close()
    } catch {
        Set-Content -LiteralPath $Path -Value ("monitor capture failed: " + $_.Exception.Message) -Encoding ASCII
    }
}

function Get-MarkerField([string]$Text, [string]$Name) {
    $boundary = '(?=(?:\s*(?:[A-Za-z][A-Za-z0-9]*=|\[)|\s*$))'
    $match = [regex]::Match($Text, [regex]::Escape($Name) + '=(?:(?<value16>[0-9A-Fa-f]{16})' + $boundary + '|(?<value8>[0-9A-Fa-f]{8})' + $boundary + ')')
    if (-not $match.Success) { return $null }
    $value = if ($match.Groups['value16'].Success) { $match.Groups['value16'].Value } else { $match.Groups['value8'].Value }
    return "0x" + $value.ToUpperInvariant()
}

$startingCommittedHead = (& git -C $root rev-parse HEAD).Trim()
$startingBranch = (& git -C $root branch --show-current).Trim()
$upstream = (& git -C $root rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>$null).Trim()
$startingWorktreeStatus = @(& git -C $root status --short)
$taskStartCheckpoint = [ordered]@{
    head=$startingCommittedHead
    branch=$startingBranch
    dirtyState=if ($startingWorktreeStatus.Count -eq 0) { "clean" } else { "dirty" }
    ordinaryKernelSha256="75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6"
    ordinaryEspSha256="75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6"
}

New-Item -ItemType Directory -Force -Path $runRoot, $buildRoot, $artifactRoot, $runtimeRoot | Out-Null
$ordinaryBuildBefore = if (Test-Path -LiteralPath $kernelPath -PathType Leaf) { Hash-File $kernelPath } else { $null }
$ordinaryEspBefore = if (Test-Path -LiteralPath $espKernelPath -PathType Leaf) { Hash-File $espKernelPath } else { $null }
$ordinaryKernelBefore = [ordered]@{ build=$ordinaryBuildBefore; esp=$ordinaryEspBefore }
if ($isC011EC26) {
    if ($ordinaryBuildBefore -eq $null -or $ordinaryEspBefore -ne $ordinaryBuildBefore) {
        throw "The C011EC26 ordinary kernel/ESP source-state artifacts do not agree. build=$ordinaryBuildBefore esp=$ordinaryEspBefore"
    }
    $normalKernelHash = $ordinaryBuildBefore
    $taskStartCheckpoint.ordinaryKernelSha256 = $ordinaryBuildBefore
    $taskStartCheckpoint.ordinaryEspSha256 = $ordinaryEspBefore
} elseif ($ordinaryBuildBefore -ne $normalKernelHash -or $ordinaryEspBefore -ne $normalKernelHash) {
    throw "The ordinary kernel/ESP is not the verified production baseline before proof deployment. build=$ordinaryBuildBefore esp=$ordinaryEspBefore expected=$normalKernelHash"
}
Copy-Item -LiteralPath $kernelPath -Destination $normalKernelSource -Force
foreach ($path in @($normalKernelSource,$bootloader,$qemu,$ovmf,$objcopy,$objdump,$readelf,$python,$converter,$vsBat,$dotnet,$activeArchive)) {
    Require-File $path "Required experiment input"
}
if ([string]::IsNullOrWhiteSpace($make)) { throw "mingw32-make.exe/make.exe was not found." }
if ((Hash-File $activeArchive) -ne $activeArchiveHash) { throw "Active PAL archive hash changed." }
$qemuVersion = (& $qemu --version | Select-Object -First 1)
Set-Content -LiteralPath (Join-Path $runRoot "qemu-version.txt") -Value $qemuVersion -Encoding ASCII

$sourceRoot = Join-Path $root "out\dotnet\pal-runtime-active-replacement-build\locked-source\src\coreclr"
$nativeAotRoot = Join-Path $sourceRoot "nativeaot"
$palSourceRoot = Join-Path $root "out\dotnet\pal-runtime-active-replacement-build\locked-source\src\native"
$lockedSourceRoot = Join-Path $root "out\dotnet\pal-runtime-active-replacement-build\locked-source"
$isC14QueueInstrumentation = $isFirstRootFirstNonNullOldO -or $isNextGenuineRootProvider
$platformSource = Join-Path $root "tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_platform.cpp"
$probeSource = Join-Path $root "tools\dotnet\runtime-pack\src\probes\guidexos_nativeaot_gc_allocation_probe.cpp"
$hostShimSource = Join-Path $root "tools\dotnet\runtime-pack\src\probes\guidexos_nativeaot_managed_host_shims.cpp"
$startupProbeSource = Join-Path $root "tools\dotnet\runtime-pack\src\probes\guidexos_nativeaot_gc_startup_probe.cpp"
$nativeUnwindPrimitiveSource = Join-Path $root "tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_amd64_unwind_primitive.cpp"
$runtimeSupportSource = Join-Path $root "samples\managed\HostLogProof\runtime_support.c"
$replacementRoot = Join-Path $root "out\dotnet\gc-first-real-allocation\identity\build1\rebuilt"
$identityManifestPath = Join-Path $root "out\dotnet\gc-first-real-allocation\identity\build1\stock\identity.json"
$palBridge = Join-Path $root "out\dotnet\pal-runtime-active-replacement-build\guidexos_nativeaot_pal_contract.bridge.obj"
$gcEnv = Join-Path $replacementRoot "guidexos_gcenv.obj"
$gcEnvEeSource = Join-Path $runtimeRoot "gcenv.ee.single-thread-suspend-ee.cpp"
$gcEnvEe = Join-Path $runtimeRoot "gcenv.ee.single-thread-suspend-ee.obj"
$gcEnumSource = Join-Path $runtimeRoot $(if ($isNextGenuineRootProvider) { "GcEnum.next-genuine-root-provider.cpp" } elseif ($isFirstRootCallbackEntry) { "GcEnum.first-root-callback-entry.cpp" } else { "GcEnum.first-root-candidate-load.cpp" })
$gcEnum = Join-Path $runtimeRoot $(if ($isNextGenuineRootProvider) { "GcEnum.next-genuine-root-provider.obj" } elseif ($isFirstRootCallbackEntry) { "GcEnum.first-root-callback-entry.obj" } else { "GcEnum.first-root-candidate-load.obj" })
$gcWksSource = Join-Path $runtimeRoot "gcwks.first-root-callback-entry.cpp"
$gcWks = Join-Path $runtimeRoot "gcwks.first-root-callback-entry.obj"
$gcCppSource = Join-Path $runtimeRoot "gc.c011ec34.cpp"
$gcCppObj = Join-Path $runtimeRoot "gc.c011ec34.obj"
$objectHandleSource = Join-Path $runtimeRoot "objecthandle.c011ec29.cpp"
$objectHandleObj = Join-Path $runtimeRoot "objecthandle.c011ec29.obj"
$objectHandleC30Source = Join-Path $runtimeRoot "objecthandle.c011ec30.cpp"
$objectHandleC30Obj = Join-Path $runtimeRoot "objecthandle.c011ec30.obj"
$handleTableScanSource = Join-Path $runtimeRoot "handletablescan.c011ec30.cpp"
$handleTableScanObj = Join-Path $runtimeRoot "handletablescan.c011ec30.obj"
$handleTableHelpersC31Source = Join-Path $runtimeRoot "HandleTableHelpers.c011ec31.cpp"
$handleTableHelpersC31Obj = Join-Path $runtimeRoot "HandleTableHelpers.c011ec31.obj"
$handleTableHelpersC32Source = Join-Path $runtimeRoot "HandleTableHelpers.c011ec32.cpp"
$handleTableHelpersC32Obj = Join-Path $runtimeRoot "HandleTableHelpers.c011ec32.obj"
$handleTableHelpersC33Source = Join-Path $runtimeRoot "HandleTableHelpers.c011ec33.cpp"
$handleTableHelpersC33Obj = Join-Path $runtimeRoot "HandleTableHelpers.c011ec33.obj"
$platformObj = Join-Path $runtimeRoot "guidexos_nativeaot_platform.single-thread-suspend-ee.obj"
$probeObj = Join-Path $runtimeRoot "guidexos_nativeaot_gc_allocation_probe.single-thread-suspend-ee.obj"
$gcBridgeBoundary = Join-Path $runtimeRoot "guidexos_nativeaot_gcenv_startup_bridge.single-thread-suspend-ee.obj"
$gcBridgeSource = Join-Path $root "tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_gcenv_startup_bridge.cpp"
$platformContract = Join-Path $gcStartupRoot "guidexos_nativeaot_gc_startup_platform_contract.obj"
$platformContractSource = Join-Path $root "tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_gc_startup_platform_contract.cpp"
$platformContractC21Obj = Join-Path $runtimeRoot "guidexos_nativeaot_gc_startup_platform_contract.c011ec21.obj"
$palStartup = Join-Path $gcStartupRoot "PalRedhawkMinWin.gc-startup.obj"
$startupDiagnostic = Join-Path $gcStartupRoot "startup-diagnostic.obj"
$gcHelpersDiagnostic = Join-Path $gcStartupRoot "gc-helpers-diagnostic.obj"
$gcHelpersC011EC18Source = Join-Path $runtimeRoot "gc-helpers-c011ec18.cpp"
$gcHelpersC011EC18Obj = Join-Path $runtimeRoot "gc-helpers-c011ec18.obj"
$stackFrameIteratorSource = Join-Path $runtimeRoot "StackFrameIterator.c011ec18.cpp"
$stackFrameIteratorObj = Join-Path $runtimeRoot "StackFrameIterator.c011ec18.obj"
$coffNativeCodeManagerSource = Join-Path $runtimeRoot "CoffNativeCodeManager.c011ec19.cpp"
$coffNativeCodeManagerObj = Join-Path $runtimeRoot "CoffNativeCodeManager.c011ec19.obj"
$gcHelpersAlign = Join-Path $gcStartupRoot "gc-helpers-align-up.obj"
$threadObj = Join-Path $oldArtifact "thread.renamed.obj"
$threadC011EC26Source = Join-Path $runtimeRoot "thread.c011ec26.cpp"
$threadC011EC26Obj = Join-Path $runtimeRoot "thread.c011ec26.obj"
$ehObj = Join-Path $oldArtifact "EHHelpers.renamed.obj"
$allocFastObj = Join-Path $oldArtifact "AllocFast.renamed.obj"
$allocFastPublicObj = Join-Path $runtimeRoot "AllocFast.c011ec18-public.obj"
$allocFastLinkObj = if ($isTransitionFrameControlPc -or $isC011EC19) { $allocFastPublicObj } else { $allocFastObj }
$runtimeSupportObj = Join-Path $artifactRoot "runtime_support.obj"
$managedRuntimePackObj = Join-Path $artifactRoot "managed-runtime-pack.c011ec18.lib"
$hostShimObj = Join-Path $artifactRoot "guidexos_nativeaot_managed_host_shims.obj"
$startupProbeObj = Join-Path $artifactRoot "guidexos_nativeaot_gc_startup_probe.managed.obj"
$nativeUnwindPrimitiveObj = Join-Path $runtimeRoot "guidexos_nativeaot_amd64_unwind_primitive.obj"
$adaptedArchive = Join-Path $artifactRoot "Runtime.WorkstationGC.guidexos-nativeaot-single-thread-suspend-ee.lib"
$manifestPath = Join-Path $runRoot "manifest.json"
$runResults = @()

foreach ($path in @($palBridge,$gcEnv,$gcBridgeSource,$platformContract,$palStartup,$startupDiagnostic,$gcHelpersDiagnostic,$gcHelpersAlign,$threadObj,$ehObj,$allocFastObj)) {
    Require-File $path "Authorized replacement input"
}
if ($isC011EC21) { Require-File $platformContractSource "C011EC21 startup-platform contract source" }
if ($isC011EC21) { $platformContract = $platformContractC21Obj }
Require-File $identityManifestPath "Authorized normalized adapted-GC identity manifest"

try {
    $repoHead = $startingCommittedHead
    $dirtyState = $startingWorktreeStatus
    $dirtySummary = (($startingWorktreeStatus) -join "`n")
    $lockedCommit = "9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3"
    $lockedEePath = Join-Path $lockedSourceRoot "src\coreclr\nativeaot\Runtime\gcenv.ee.cpp"
    Require-File $lockedEePath "Locked NativeAOT EE source"
    $lockedEeText = Get-Content -LiteralPath $lockedEePath -Raw
    if ($isFirstPerThreadRootProvider) {
        $storageObserverAttribute = if ($isCandidateLoadEnumeration) { "" } else { "__declspec(noreturn) " }
        $declaration = @'
extern "C" void __cdecl guideXosNativeAotSuspendEeEntry(uint32_t reason);
extern "C" void __cdecl guideXosNativeAotSuspendEeAfterLock();
extern "C" void __cdecl guideXosNativeAotSuspendEeAfterSuspend();
extern "C" void __cdecl guideXosNativeAotSuspendEeBodyReturn();
extern "C" void __cdecl guideXosNativeAotDisablePreemptiveEntry();
extern "C" void __cdecl guideXosNativeAotDisablePreemptiveReturn();
extern "C" void __cdecl guideXosNativeAotAllocationContextFixupGcStartWorkObserver();
extern "C" void __cdecl guideXosNativeAotAllocationRootPhaseRequested();
extern "C" void __cdecl guideXosNativeAotAllocationContextFixupEnumerationEntry();
extern "C" void __cdecl guideXosNativeAotAllocationContextFixupContextVisited(uintptr_t context);
extern "C" void __cdecl guideXosNativeAotAllocationContextFixupEnumerationComplete();
extern "C" void __cdecl guideXosNativeAotFirstPerThreadRootGcScanRootsEntered(int condemned, int max_gen, uintptr_t scanContext);
extern "C" void __cdecl guideXosNativeAotFirstPerThreadRootForeachThreadEntered();
extern "C" void __cdecl guideXosNativeAotFirstPerThreadRootIteratorInitialized();
extern "C" void __cdecl guideXosNativeAotFirstPerThreadRootIteratorCompletion();
extern "C" void __cdecl guideXosNativeAotFirstPerThreadRootThreadEnumerated(uintptr_t thread);
extern "C" void __cdecl guideXosNativeAotFirstPerThreadRootThreadExcluded(uintptr_t thread, uint32_t reason);
extern "C" void __cdecl guideXosNativeAotFirstPerThreadRootThreadIncluded(uintptr_t thread);
extern "C" void __cdecl guideXosNativeAotFirstPerThreadRootThreadStaticListObserved(uintptr_t thread, uintptr_t list);
extern "C" STORAGE_OBSERVER_ATTRIBUTEvoid __cdecl guideXosNativeAotFirstPerThreadRootThreadStaticStorageEntered(uintptr_t thread, uintptr_t storage);
'@
        $declaration = $declaration.Replace('STORAGE_OBSERVER_ATTRIBUTE', $storageObserverAttribute)
    } elseif ($isAllocationContextFixupRootBoundary) {
        $declaration = @'
extern "C" void __cdecl guideXosNativeAotSuspendEeEntry(uint32_t reason);
extern "C" void __cdecl guideXosNativeAotSuspendEeAfterLock();
extern "C" void __cdecl guideXosNativeAotSuspendEeAfterSuspend();
extern "C" void __cdecl guideXosNativeAotSuspendEeBodyReturn();
extern "C" void __cdecl guideXosNativeAotDisablePreemptiveEntry();
extern "C" void __cdecl guideXosNativeAotDisablePreemptiveReturn();
extern "C" void __cdecl guideXosNativeAotAllocationContextFixupGcStartWorkObserver();
extern "C" void __cdecl guideXosNativeAotAllocationRootPhaseRequested();
extern "C" __declspec(noreturn) void __cdecl guideXosNativeAotAllocationContextFixupRootBoundary();
extern "C" void __cdecl guideXosNativeAotAllocationContextFixupEnumerationEntry();
extern "C" void __cdecl guideXosNativeAotAllocationContextFixupContextVisited(uintptr_t context);
extern "C" void __cdecl guideXosNativeAotAllocationContextFixupEnumerationComplete();
'@
    } else {
        $declaration = @'
extern "C" void __cdecl guideXosNativeAotSuspendEeEntry(uint32_t reason);
extern "C" void __cdecl guideXosNativeAotSuspendEeAfterLock();
extern "C" void __cdecl guideXosNativeAotSuspendEeAfterSuspend();
extern "C" void __cdecl guideXosNativeAotSuspendEeBodyReturn();
extern "C" void __cdecl guideXosNativeAotDisablePreemptiveEntry();
extern "C" __declspec(noreturn) void __cdecl guideXosNativeAotDisablePreemptiveReturn();
extern "C" __declspec(noreturn) void __cdecl guideXosNativeAotSuspendEeGcStartWorkBoundary();
'@
    }
    if ($isNextGenuineRootProvider) {
        $declaration += [Environment]::NewLine + @'
extern "C" void __cdecl guideXosNativeAotC011EC15GcScanRootsEntered(int condemned, int maxGeneration, uintptr_t scanContext);
extern "C" void __cdecl guideXosNativeAotC011EC15ProviderEntered(uint32_t category, uintptr_t thread, uintptr_t provider);
'@.TrimEnd()
    }
    if ($isC011EC26) {
        $declaration += [Environment]::NewLine + @'
extern "C" void __cdecl guideXosNativeAotC011EC26GcScanRootsEntered(int condemned, int maxGeneration, uintptr_t scanContext);
extern "C" void __cdecl guideXosNativeAotC011EC26StackProviderReturned();
extern "C" void __cdecl guideXosNativeAotC011EC26GcScanRootsReturned();
extern "C" void __cdecl guideXosNativeAotC011EC26PostScanAfterGcScanRootsEntered();
'@.TrimEnd()
        if ($isC011EC33) {
            $declaration += [Environment]::NewLine + @'
extern "C" void __cdecl guideXosNativeAotC011EC33GcScanRootsEntered(int condemned, int maxGeneration, uintptr_t scanContext);
extern "C" void __cdecl guideXosNativeAotC011EC33GcScanRootsReturned();
extern "C" void __cdecl guideXosNativeAotC011EC33AfterGcScanRootsEntered(int condemned, int maxGeneration, uintptr_t scanContext);
extern "C" void __cdecl guideXosNativeAotC011EC33AfterGcScanRootsReturned();
extern "C" void __cdecl guideXosNativeAotC011EC33GcDoneEntered(int condemned);
extern "C" void __cdecl guideXosNativeAotC011EC33RestartEEEntered(int finishedGc);
extern "C" void __cdecl guideXosNativeAotC011EC33RestartEEReturned(int finishedGc);
'@.TrimEnd()
            if ($isC011EC34) {
                $declaration += [Environment]::NewLine + @'
extern "C" void __cdecl guideXosNativeAotC011EC34GcScanRootsEntered(int condemned, int maxGeneration, uintptr_t scanContext, uintptr_t callback);
extern "C" void __cdecl guideXosNativeAotC011EC34GcScanRootsReturned();
'@.TrimEnd()
            }
        }
        if ($isC011EC29) {
            $declaration += [Environment]::NewLine + @'
extern "C" void __cdecl guideXosNativeAotC011EC29AfterGcScanRootsEntered(int condemned, int maxGeneration, uintptr_t scanContext);
extern "C" void __cdecl guideXosNativeAotC011EC29AfterGcScanRootsReturned();
'@.TrimEnd()
        } elseif ($isC011EC28) {
            $declaration += [Environment]::NewLine + @'
extern "C" __declspec(noreturn) void __cdecl guideXosNativeAotC011EC28DrainBoundaryEntered();
'@.TrimEnd()
        } elseif ($isC011EC27Stop) {
            $declaration += [Environment]::NewLine + @'
extern "C" __declspec(noreturn) void __cdecl guideXosNativeAotC011EC27PostRootAfterGcScanRootsEntered();
'@.TrimEnd()
        }
    }
    $lockedEeText = $lockedEeText.Replace('#include "volatile.h"', '#include "volatile.h"' + [Environment]::NewLine + [Environment]::NewLine + $declaration.TrimEnd())
    $suspendPattern = '(?m)^void GCToEEInterface::SuspendEE\(SUSPEND_REASON reason\)\r?\n\{'
    $suspendReplacement = 'void GCToEEInterface::SuspendEE(SUSPEND_REASON reason)' + [Environment]::NewLine + '{' + [Environment]::NewLine + '    guideXosNativeAotSuspendEeEntry((uint32_t)reason);'
    $injectedText = [regex]::Replace($lockedEeText, $suspendPattern, $suspendReplacement, 1)
    $injectedText = $injectedText.Replace('    GetThreadStore()->LockThreadStore();', '    GetThreadStore()->LockThreadStore();' + [Environment]::NewLine + '    guideXosNativeAotSuspendEeAfterLock();')
    $injectedText = $injectedText.Replace('    GetThreadStore()->SuspendAllThreads(true);', '    GetThreadStore()->SuspendAllThreads(true);' + [Environment]::NewLine + '    guideXosNativeAotSuspendEeAfterSuspend();')
    $injectedText = $injectedText.Replace('    FireEtwGCSuspendEEEnd_V1(GetClrInstanceId());', '    FireEtwGCSuspendEEEnd_V1(GetClrInstanceId());' + [Environment]::NewLine + '    guideXosNativeAotSuspendEeBodyReturn();')
    $disablePattern = '(?m)^void GCToEEInterface::DisablePreemptiveGC\(\)\r?\n\{'
    $disableReplacement = 'void GCToEEInterface::DisablePreemptiveGC()' + [Environment]::NewLine + '{' + [Environment]::NewLine + '    guideXosNativeAotDisablePreemptiveEntry();'
    $injectedText = [regex]::Replace($injectedText, $disablePattern, $disableReplacement, 1)
    $injectedText = $injectedText.Replace('    ThreadStore::GetCurrentThread()->DisablePreemptiveMode();', '    ThreadStore::GetCurrentThread()->DisablePreemptiveMode();' + [Environment]::NewLine + '    guideXosNativeAotDisablePreemptiveReturn();')
    if ($isC011EC33) {
        $restartPattern = '(?m)^void GCToEEInterface::RestartEE\(bool /\*bFinishedGC\*/\)\r?\n\{'
        $restartReplacement = 'void GCToEEInterface::RestartEE(bool /*bFinishedGC*/)' + [Environment]::NewLine + '{' + [Environment]::NewLine + '    guideXosNativeAotC011EC33RestartEEEntered(1);'
        $injectedText = [regex]::Replace($injectedText, $restartPattern, $restartReplacement, 1)
        $injectedText = $injectedText.Replace(
            '    FireEtwGCRestartEEEnd_V1(GetClrInstanceId());',
            '    FireEtwGCRestartEEEnd_V1(GetClrInstanceId());' + [Environment]::NewLine + '    guideXosNativeAotC011EC33RestartEEReturned(1);')
        $injectedText = $injectedText.Replace(
            'void GCToEEInterface::GcDone(int condemned)' + [Environment]::NewLine + '{',
            'void GCToEEInterface::GcDone(int condemned)' + [Environment]::NewLine + '{' + [Environment]::NewLine + '    guideXosNativeAotC011EC33GcDoneEntered(condemned);')
        if ($injectedText -notmatch 'guideXosNativeAotC011EC33RestartEEEntered' -or
            $injectedText -notmatch 'guideXosNativeAotC011EC33RestartEEReturned' -or
            $injectedText -notmatch 'guideXosNativeAotC011EC33GcDoneEntered') {
            throw "C011EC33 GC completion observers were not inserted."
        }
    }
    if ($isFirstPerThreadRootProvider) {
        $gcStartPattern = '(?m)^void GCToEEInterface::GcStartWork\(int condemned, int /\*max_gen\*/\)\r?\n\{'
        $gcStartReplacement = 'void GCToEEInterface::GcStartWork(int condemned, int /*max_gen*/)' + [Environment]::NewLine + '{' + [Environment]::NewLine + '    guideXosNativeAotAllocationContextFixupGcStartWorkObserver();'
        $injectedText = [regex]::Replace($injectedText, $gcStartPattern, $gcStartReplacement, 1)
        $beforeRootsPattern = '(?m)^void GCToEEInterface::BeforeGcScanRoots\(int condemned, bool is_bgc, bool is_concurrent\)\r?\n\{'
        $beforeRootsReplacement = 'void GCToEEInterface::BeforeGcScanRoots(int condemned, bool is_bgc, bool is_concurrent)' + [Environment]::NewLine + '{' + [Environment]::NewLine + '    guideXosNativeAotAllocationRootPhaseRequested();'
        $injectedText = [regex]::Replace($injectedText, $beforeRootsPattern, $beforeRootsReplacement, 1)
        $scanRootsPattern = '(?m)^void GCToEEInterface::GcScanRoots\(ScanFunc\* fn, int condemned, int max_gen, ScanContext\* sc\)\r?\n\{'
        $scanRootsReplacement = 'void GCToEEInterface::GcScanRoots(ScanFunc* fn, int condemned, int max_gen, ScanContext* sc)' + [Environment]::NewLine + '{' + [Environment]::NewLine + '    guideXosNativeAotFirstPerThreadRootGcScanRootsEntered(condemned, max_gen, reinterpret_cast<uintptr_t>(sc));'
        $injectedText = [regex]::Replace($injectedText, $scanRootsPattern, $scanRootsReplacement, 1)
        if ($isNextGenuineRootProvider) {
            $injectedText = $injectedText.Replace(
                '    guideXosNativeAotFirstPerThreadRootGcScanRootsEntered(condemned, max_gen, reinterpret_cast<uintptr_t>(sc));',
                '    guideXosNativeAotFirstPerThreadRootGcScanRootsEntered(condemned, max_gen, reinterpret_cast<uintptr_t>(sc));' + [Environment]::NewLine + '    guideXosNativeAotC011EC15GcScanRootsEntered(condemned, max_gen, reinterpret_cast<uintptr_t>(sc));')
        }
        if ($isC011EC26) {
            $injectedText = $injectedText.Replace(
                '    guideXosNativeAotC011EC15GcScanRootsEntered(condemned, max_gen, reinterpret_cast<uintptr_t>(sc));',
                '    guideXosNativeAotC011EC15GcScanRootsEntered(condemned, max_gen, reinterpret_cast<uintptr_t>(sc));' + [Environment]::NewLine + '    guideXosNativeAotC011EC26GcScanRootsEntered(condemned, max_gen, reinterpret_cast<uintptr_t>(sc));')
            if ($isC011EC33) {
                $injectedText = $injectedText.Replace(
                    '    guideXosNativeAotC011EC26GcScanRootsEntered(condemned, max_gen, reinterpret_cast<uintptr_t>(sc));',
                    '    guideXosNativeAotC011EC26GcScanRootsEntered(condemned, max_gen, reinterpret_cast<uintptr_t>(sc));' + [Environment]::NewLine + '    guideXosNativeAotC011EC33GcScanRootsEntered(condemned, max_gen, reinterpret_cast<uintptr_t>(sc));')
                if ($isC011EC34) {
                    $injectedText = $injectedText.Replace(
                        '    guideXosNativeAotC011EC33GcScanRootsEntered(condemned, max_gen, reinterpret_cast<uintptr_t>(sc));',
                        '    guideXosNativeAotC011EC33GcScanRootsEntered(condemned, max_gen, reinterpret_cast<uintptr_t>(sc));' + [Environment]::NewLine + '    guideXosNativeAotC011EC34GcScanRootsEntered(condemned, max_gen, reinterpret_cast<uintptr_t>(sc), reinterpret_cast<uintptr_t>(fn));')
                }
            }
        }
        $enumPattern = '(?m)^void GCToEEInterface::GcEnumAllocContexts\(enum_alloc_context_func\* fn, void\* param\)\r?\n\{'
        $enumReplacement = 'void GCToEEInterface::GcEnumAllocContexts(enum_alloc_context_func* fn, void* param)' + [Environment]::NewLine + '{' + [Environment]::NewLine + '    guideXosNativeAotAllocationContextFixupEnumerationEntry();'
        $injectedText = [regex]::Replace($injectedText, $enumPattern, $enumReplacement, 1)
        $injectedText = $injectedText.Replace('        (*fn) (thread->GetAllocContext(), param);', '        guideXosNativeAotAllocationContextFixupContextVisited(reinterpret_cast<uintptr_t>(thread->GetAllocContext()));' + [Environment]::NewLine + '        (*fn) (thread->GetAllocContext(), param);')
        $enumEnd = '    END_FOREACH_THREAD' + [Environment]::NewLine + '}'
        $injectedText = $injectedText.Replace($enumEnd, '    END_FOREACH_THREAD' + [Environment]::NewLine + '    guideXosNativeAotAllocationContextFixupEnumerationComplete();' + [Environment]::NewLine + '}')

        $foreachOpen = '    FOREACH_THREAD(pThread)' + [Environment]::NewLine + '    {'
        $foreachReplacement = @"
    guideXosNativeAotFirstPerThreadRootForeachThreadEntered();
    {
        ThreadStore::Iterator __threads;
        guideXosNativeAotFirstPerThreadRootIteratorInitialized();
        Thread* pThread;
        while ((pThread = __threads.GetNext()) != NULL)
        {
            guideXosNativeAotFirstPerThreadRootThreadEnumerated(reinterpret_cast<uintptr_t>(pThread));
"@
        if (-not $injectedText.Contains($foreachOpen)) { throw "Locked GcScanRoots FOREACH_THREAD opening was not found." }
        $injectedText = $injectedText.Replace($foreachOpen, $foreachReplacement.TrimEnd())
        $injectedText = $injectedText.Replace('        if (pThread->IsGCSpecial())' + [Environment]::NewLine + '            continue;', '        if (pThread->IsGCSpecial())' + [Environment]::NewLine + '        {' + [Environment]::NewLine + '            guideXosNativeAotFirstPerThreadRootThreadExcluded(reinterpret_cast<uintptr_t>(pThread), 1u);' + [Environment]::NewLine + '            continue;' + [Environment]::NewLine + '        }')
        $injectedText = $injectedText.Replace('            InlinedThreadStaticRoot* pRoot = pThread->GetInlinedThreadStaticList();', '            guideXosNativeAotFirstPerThreadRootThreadIncluded(reinterpret_cast<uintptr_t>(pThread));' + [Environment]::NewLine + '            InlinedThreadStaticRoot* pRoot = pThread->GetInlinedThreadStaticList();' + [Environment]::NewLine + '            guideXosNativeAotFirstPerThreadRootThreadStaticListObserved(reinterpret_cast<uintptr_t>(pThread), reinterpret_cast<uintptr_t>(pRoot));')
        if ($isNextGenuineRootProvider) {
            $injectedText = $injectedText.Replace(
                '                EnumGcRef(&pRoot->m_threadStaticsBase, GCRK_Object, fn, sc);',
                '                guideXosNativeAotC011EC15ProviderEntered(1u, reinterpret_cast<uintptr_t>(pThread), reinterpret_cast<uintptr_t>(pRoot));' + [Environment]::NewLine + '                EnumGcRef(&pRoot->m_threadStaticsBase, GCRK_Object, fn, sc);')
        }
        $injectedText = $injectedText.Replace('            EnumGcRef(pThread->GetThreadStaticStorage(), GCRK_Object, fn, sc);', '            Object** threadStaticStorage = pThread->GetThreadStaticStorage();' + [Environment]::NewLine + '            guideXosNativeAotFirstPerThreadRootThreadStaticStorageEntered(reinterpret_cast<uintptr_t>(pThread), reinterpret_cast<uintptr_t>(threadStaticStorage));' + [Environment]::NewLine + '            EnumGcRef(threadStaticStorage, GCRK_Object, fn, sc);')
         if ($isNextGenuineRootProvider) {
             $injectedText = $injectedText.Replace(
                 '            EnumGcRef(threadStaticStorage, GCRK_Object, fn, sc);',
                 '            guideXosNativeAotC011EC15ProviderEntered(2u, reinterpret_cast<uintptr_t>(pThread), reinterpret_cast<uintptr_t>(threadStaticStorage));' + [Environment]::NewLine + '            EnumGcRef(threadStaticStorage, GCRK_Object, fn, sc);')
             $injectedText = $injectedText.Replace(
                  '            STRESS_LOG1(LF_GC | LF_GCROOTS, LL_INFO100, "{ Starting scan of Thread %p\n", pThread);',
                  '            guideXosNativeAotC011EC15ProviderEntered(3u, reinterpret_cast<uintptr_t>(pThread), reinterpret_cast<uintptr_t>(pThread));' + [Environment]::NewLine + '            STRESS_LOG1(LF_GC | LF_GCROOTS, LL_INFO100, "{ Starting scan of Thread %p\n", pThread);')
            $injectedText = $injectedText.Replace(
                '            guideXosNativeAotC011EC15ProviderEntered(3u, reinterpret_cast<uintptr_t>(pThread), reinterpret_cast<uintptr_t>(pThread));' + [Environment]::NewLine + '            pThread->GcScanRoots(fn, sc);',
                '            pThread->GcScanRoots(fn, sc);' + [Environment]::NewLine + $(if ($isC011EC26) { '            guideXosNativeAotC011EC26StackProviderReturned();' } else { '' }))
        }
        if ($isC011EC26) {
            $stackProviderReturnNeedle = '            pThread->GcScanRoots(fn, sc);'
            if (-not $injectedText.Contains($stackProviderReturnNeedle)) {
                throw "C011EC26 stack-provider return point was not found."
            }
            $injectedText = Replace-First $injectedText $stackProviderReturnNeedle (
                $stackProviderReturnNeedle + [Environment]::NewLine +
                '            guideXosNativeAotC011EC26StackProviderReturned();')
        }
        $injectedText = $injectedText.Replace('    END_FOREACH_THREAD' + [Environment]::NewLine + [Environment]::NewLine + '    sc->thread_under_crawl = NULL;', '        guideXosNativeAotFirstPerThreadRootIteratorCompletion();' + [Environment]::NewLine + '    }' + [Environment]::NewLine + [Environment]::NewLine + '    sc->thread_under_crawl = NULL;' + $(if ($isC011EC26) { [Environment]::NewLine + '    guideXosNativeAotC011EC26GcScanRootsReturned();' } else { '' }) + $(if ($isC011EC33) { [Environment]::NewLine + '    guideXosNativeAotC011EC33GcScanRootsReturned();' } else { '' }) + $(if ($isC011EC34) { [Environment]::NewLine + '    guideXosNativeAotC011EC34GcScanRootsReturned();' } else { '' }))
        if ($injectedText -eq $lockedEeText -or
            $injectedText -notmatch 'guideXosNativeAotFirstPerThreadRootGcScanRootsEntered' -or
            $injectedText -notmatch 'ThreadStore::Iterator __threads' -or
            $injectedText -notmatch 'guideXosNativeAotFirstPerThreadRootThreadEnumerated' -or
            $injectedText -notmatch 'guideXosNativeAotFirstPerThreadRootThreadStaticListObserved' -or
            $injectedText -notmatch 'guideXosNativeAotFirstPerThreadRootThreadStaticStorageEntered') {
            throw "Locked gcenv.ee.cpp first-per-thread-root-provider injection did not match all required boundaries."
        }
        if ($isC011EC26) {
            $afterScanPattern = '(?m)^void GCToEEInterface::AfterGcScanRoots\(int condemned, int /\*max_gen\*/, ScanContext\* sc\)\r?\n\{'
            $afterScanMaxGenerationParameter = if ($isC011EC29) { 'int max_gen' } else { 'int /*max_gen*/' }
            $afterScanReplacement = 'void GCToEEInterface::AfterGcScanRoots(int condemned, ' + $afterScanMaxGenerationParameter + ', ScanContext* sc)' + [Environment]::NewLine + '{' + [Environment]::NewLine + '    guideXosNativeAotC011EC26PostScanAfterGcScanRootsEntered();' + $(if ($isC011EC33) { [Environment]::NewLine + '    guideXosNativeAotC011EC33AfterGcScanRootsEntered(condemned, max_gen, reinterpret_cast<uintptr_t>(sc));' } else { '' }) + $(if ($isC011EC29) { [Environment]::NewLine + '    guideXosNativeAotC011EC29AfterGcScanRootsEntered(condemned, max_gen, reinterpret_cast<uintptr_t>(sc));' } elseif ($isC011EC28) { [Environment]::NewLine + '    guideXosNativeAotC011EC28DrainBoundaryEntered();' } elseif ($isC011EC27Stop) { [Environment]::NewLine + '    guideXosNativeAotC011EC27PostRootAfterGcScanRootsEntered();' } else { '' })
            $injectedText = [regex]::Replace($injectedText, $afterScanPattern, $afterScanReplacement, 1)
            if ($isC011EC29) {
                $afterScanReturnPattern = '(?ms)(void GCToEEInterface::AfterGcScanRoots\(int condemned, int (?:/\*max_gen\*/|max_gen), ScanContext\* sc\)\r?\n\{.*?)(\r?\n\})'
                $afterScanReturnReplacement = '$1' + [Environment]::NewLine + '    guideXosNativeAotC011EC29AfterGcScanRootsReturned();' + $(if ($isC011EC33) { [Environment]::NewLine + '    guideXosNativeAotC011EC33AfterGcScanRootsReturned();' } else { '' }) + '$2'
                $injectedText = [regex]::Replace($injectedText, $afterScanReturnPattern, $afterScanReturnReplacement, 1)
                if ($injectedText -notmatch 'guideXosNativeAotC011EC29AfterGcScanRootsReturned\(\);') {
                    throw "C011EC29 AfterGcScanRoots return observer was not inserted."
                }
            }
        }
    } elseif ($isAllocationContextFixupRootBoundary) {
        $gcStartPattern = '(?m)^void GCToEEInterface::GcStartWork\(int condemned, int /\*max_gen\*/\)\r?\n\{'
        $gcStartReplacement = 'void GCToEEInterface::GcStartWork(int condemned, int /*max_gen*/)' + [Environment]::NewLine + '{' + [Environment]::NewLine + '    guideXosNativeAotAllocationContextFixupGcStartWorkObserver();'
        $injectedText = [regex]::Replace($injectedText, $gcStartPattern, $gcStartReplacement, 1)
        $beforeRootsPattern = '(?m)^void GCToEEInterface::BeforeGcScanRoots\(int condemned, bool is_bgc, bool is_concurrent\)\r?\n\{'
        $beforeRootsReplacement = 'void GCToEEInterface::BeforeGcScanRoots(int condemned, bool is_bgc, bool is_concurrent)' + [Environment]::NewLine + '{' + [Environment]::NewLine + '    guideXosNativeAotAllocationRootPhaseRequested();'
        $injectedText = [regex]::Replace($injectedText, $beforeRootsPattern, $beforeRootsReplacement, 1)
        $scanRootsPattern = '(?m)^void GCToEEInterface::GcScanRoots\(ScanFunc\* fn, int condemned, int max_gen, ScanContext\* sc\)\r?\n\{'
        $scanRootsReplacement = 'void GCToEEInterface::GcScanRoots(ScanFunc* fn, int condemned, int max_gen, ScanContext* sc)' + [Environment]::NewLine + '{' + [Environment]::NewLine + '    guideXosNativeAotAllocationContextFixupRootBoundary();'
        $injectedText = [regex]::Replace($injectedText, $scanRootsPattern, $scanRootsReplacement, 1)
        $enumPattern = '(?m)^void GCToEEInterface::GcEnumAllocContexts\(enum_alloc_context_func\* fn, void\* param\)\r?\n\{'
        $enumReplacement = 'void GCToEEInterface::GcEnumAllocContexts(enum_alloc_context_func* fn, void* param)' + [Environment]::NewLine + '{' + [Environment]::NewLine + '    guideXosNativeAotAllocationContextFixupEnumerationEntry();'
        $injectedText = [regex]::Replace($injectedText, $enumPattern, $enumReplacement, 1)
        $injectedText = $injectedText.Replace('        (*fn) (thread->GetAllocContext(), param);', '        guideXosNativeAotAllocationContextFixupContextVisited(reinterpret_cast<uintptr_t>(thread->GetAllocContext()));' + [Environment]::NewLine + '        (*fn) (thread->GetAllocContext(), param);')
        $enumEnd = '    END_FOREACH_THREAD' + [Environment]::NewLine + '}'
        $injectedText = $injectedText.Replace($enumEnd, '    END_FOREACH_THREAD' + [Environment]::NewLine + '    guideXosNativeAotAllocationContextFixupEnumerationComplete();' + [Environment]::NewLine + '}')
        if ($injectedText -eq $lockedEeText -or
            $injectedText -notmatch 'guideXosNativeAotAllocationContextFixupEnumerationEntry' -or
            $injectedText -notmatch 'guideXosNativeAotAllocationContextFixupContextVisited' -or
            $injectedText -notmatch 'guideXosNativeAotAllocationContextFixupEnumerationComplete' -or
            $injectedText -notmatch 'guideXosNativeAotAllocationContextFixupGcStartWorkObserver' -or
            $injectedText -notmatch 'guideXosNativeAotAllocationRootPhaseRequested' -or
            $injectedText -notmatch 'guideXosNativeAotAllocationContextFixupRootBoundary') {
            throw "Locked gcenv.ee.cpp allocation-context/root-boundary injection did not match all required boundaries."
        }
    } else {
        $gcStartPattern = '(?m)^void GCToEEInterface::GcStartWork\(int condemned, int /\*max_gen\*/\)\r?\n\{'
        $gcStartReplacement = 'void GCToEEInterface::GcStartWork(int condemned, int /*max_gen*/)' + [Environment]::NewLine + '{' + [Environment]::NewLine + '    guideXosNativeAotSuspendEeGcStartWorkBoundary();'
        $injectedText = [regex]::Replace($injectedText, $gcStartPattern, $gcStartReplacement, 1)
        if ($injectedText -eq $lockedEeText -or
            $injectedText -notmatch 'guideXosNativeAotSuspendEeEntry' -or
            $injectedText -notmatch 'guideXosNativeAotSuspendEeAfterLock' -or
            $injectedText -notmatch 'guideXosNativeAotSuspendEeAfterSuspend' -or
            $injectedText -notmatch 'guideXosNativeAotSuspendEeBodyReturn' -or
            $injectedText -notmatch 'guideXosNativeAotDisablePreemptiveEntry' -or
            $injectedText -notmatch 'guideXosNativeAotDisablePreemptiveReturn' -or
            $injectedText -notmatch 'guideXosNativeAotSuspendEeGcStartWorkBoundary') {
            throw "Locked gcenv.ee.cpp injection did not match all required boundaries."
        }
    }
    Set-Content -LiteralPath $gcEnvEeSource -Value $injectedText -Encoding ASCII

    if ($isTransitionFrameControlPc -or $isC011EC19) {
        $lockedGcHelpersPath = Join-Path $lockedSourceRoot "src\coreclr\nativeaot\Runtime\GCHelpers.cpp"
        $lockedStackFrameIteratorPath = Join-Path $lockedSourceRoot "src\coreclr\nativeaot\Runtime\StackFrameIterator.cpp"
        Require-File $lockedGcHelpersPath "Locked NativeAOT GCHelpers.cpp source"
        Require-File $lockedStackFrameIteratorPath "Locked NativeAOT StackFrameIterator.cpp source"

        $gcHelpersText = (Get-Content -LiteralPath $lockedGcHelpersPath -Raw).Replace("`r`n", "`n")
        $gcHelpersDeclaration = @'
extern "C" void __cdecl guideXosNativeAotC011EC18RhpGcAllocEntered(
    uintptr_t frameAddress, uintptr_t eeType, uintptr_t flags,
    uintptr_t numElements, uintptr_t threadAddress);
'@
        $gcHelpersText = $gcHelpersText.Replace(
            '#include "interoplibinterface.h"',
            '#include "interoplibinterface.h"' + [Environment]::NewLine + $gcHelpersDeclaration.TrimEnd())
        $gcHelpersPattern = '(?s)(EXTERN_C void\* F_CALL_CONV RhpGcAlloc\(MethodTable\* pEEType, uint32_t uFlags, uintptr_t numElements, PInvokeTransitionFrame\* pTransitionFrame\)\s*\{\s*)(Thread\* pThread = ThreadStore::GetCurrentThread\(\);)'
        $gcHelpersReplacement = '$1$2' + [Environment]::NewLine + '    guideXosNativeAotC011EC18RhpGcAllocEntered(' + [Environment]::NewLine + '        reinterpret_cast<uintptr_t>(pTransitionFrame), reinterpret_cast<uintptr_t>(pEEType),' + [Environment]::NewLine + '        static_cast<uintptr_t>(uFlags), numElements, reinterpret_cast<uintptr_t>(pThread));'
        $gcHelpersText = [regex]::Replace($gcHelpersText, $gcHelpersPattern, $gcHelpersReplacement, 1)
        if ($gcHelpersText -notmatch 'guideXosNativeAotC011EC18RhpGcAllocEntered') {
            throw "C011EC18 GCHelpers.cpp injection did not match RhpGcAlloc."
        }
        Set-Content -LiteralPath $gcHelpersC011EC18Source -Value $gcHelpersText -Encoding ASCII

        $stackFrameIteratorText = (Get-Content -LiteralPath $lockedStackFrameIteratorPath -Raw).Replace("`r`n", "`n")
        $stackFrameIteratorDeclarations = @'

extern "C" void __cdecl guideXosNativeAotC011EC18IteratorInitial(
    uintptr_t frameAddress, uintptr_t controlPc, uintptr_t sp,
    uintptr_t fp, uintptr_t flags);
extern "C" void __cdecl guideXosNativeAotC011EC18IteratorCodeManagerLookup(
    uintptr_t controlPc, uintptr_t sp, uintptr_t fp, uintptr_t manager);
extern "C" void __cdecl guideXosNativeAotC011EC18IteratorFindMethodInfo(
    uintptr_t controlPc, uintptr_t methodInfo, uintptr_t found);
extern "C" void __cdecl guideXosNativeAotC011EC18IteratorFramePointer(uintptr_t framePointer);
extern "C" void __cdecl guideXosNativeAotC011EC18IteratorUnwind(
    uintptr_t controlPc, uintptr_t sp);
'@
        if ($isC011EC23) {
            $stackFrameIteratorDeclarations += @'
extern "C" uint32_t __cdecl guideXosNativeAotC011EC23TryNativeUnwind(
    uintptr_t controlPc, uintptr_t regDisplay);
'@
        }
        if ($isC011EC26) {
            $stackFrameIteratorDeclarations += @'
extern "C" void __cdecl guideXosNativeAotC011EC26IteratorCompleted();
'@
        }
        $stackFrameIteratorText = $stackFrameIteratorText.Replace(
            '#include "StackFrameIterator.h"',
            '#include "StackFrameIterator.h"' + $stackFrameIteratorDeclarations.TrimEnd())
        if ($isC011EC23) {
            $nativeMethodStateNeedle = @'
    if ((m_dwFlags & (SkipNativeFrames|UnwoundReversePInvoke)) == UnwoundReversePInvoke)
    {
        // There is no implementation of ICodeManager for native code.
        m_pCodeManager = nullptr;
        m_effectiveSafePointAddress = nullptr;
        m_FramePointer = nullptr;
        m_dwFlags |= MethodStateCalculated;
        return;
    }
'@
            $nativeMethodStateReplacement = @'
    const bool guideXosNativeFrameCandidate =
        ((m_dwFlags & (SkipNativeFrames|UnwoundReversePInvoke)) == UnwoundReversePInvoke) ||
        (m_pCodeManager != NULL && !m_pInstance->IsManaged(m_ControlPC));
    if (guideXosNativeFrameCandidate)
    {
        const uint32_t guideXosNativeUnwindResult =
            guideXosNativeAotC011EC23TryNativeUnwind(
                reinterpret_cast<uintptr_t>(m_ControlPC),
                reinterpret_cast<uintptr_t>(&m_RegDisplay));
        if (guideXosNativeUnwindResult == 1u)
        {
            SetControlPC(dac_cast<PTR_VOID>(
                PCODEToPINSTR(m_RegDisplay.GetIP())));
            m_pPreviousTransitionFrame = nullptr;
            m_dwFlags &= ~UnwoundReversePInvoke;
        }
#if defined(GUIDEXOS_NATIVEAOT_C011EC26_STACK_COMPLETION)
        else if (guideXosNativeUnwindResult == 2u)
        {
            SetControlPC(0);
            guideXosNativeAotC011EC26IteratorCompleted();
            m_pCodeManager = nullptr;
            m_effectiveSafePointAddress = nullptr;
            m_FramePointer = nullptr;
            m_dwFlags |= MethodStateCalculated;
            return;
        }
#endif
        else
        {
            // Native frames have no ICodeManager.  The PAL provider either
            // stopped safely after a bounded native chain or left the frame
            // unavailable; neither case is a managed frame.
            m_pCodeManager = nullptr;
            m_effectiveSafePointAddress = nullptr;
            m_FramePointer = nullptr;
            m_dwFlags |= MethodStateCalculated;
            return;
        }
    }
'@
            $nativeMethodStateNeedle = $nativeMethodStateNeedle.Replace("`r`n", "`n")
            $nativeMethodStateReplacement = $nativeMethodStateReplacement.Replace("`r`n", "`n")
            if (-not $stackFrameIteratorText.Contains($nativeMethodStateNeedle)) {
                throw "C011EC23 StackFrameIterator native-method-state injection point was not found."
            }
            $stackFrameIteratorText = Replace-First $stackFrameIteratorText $nativeMethodStateNeedle $nativeMethodStateReplacement
        }
        $iteratorInitial = @'
    if (pFrame->m_Flags & PTFF_SAVE_RSP)  { m_RegDisplay.SP   = *pPreservedRegsCursor++; }
    guideXosNativeAotC011EC18IteratorInitial(
        reinterpret_cast<uintptr_t>(pFrame),
        reinterpret_cast<uintptr_t>(m_ControlPC),
        static_cast<uintptr_t>(m_RegDisplay.GetSP()),
        static_cast<uintptr_t>(m_RegDisplay.GetFP()),
        static_cast<uintptr_t>(pFrame->m_Flags));
'@
        $iteratorInitialNeedle = '    if (pFrame->m_Flags & PTFF_SAVE_RSP)  { m_RegDisplay.SP   = *pPreservedRegsCursor++; }'
        if (-not $stackFrameIteratorText.Contains($iteratorInitialNeedle)) {
            throw "C011EC18 StackFrameIterator initial SP injection point was not found."
        }
        $stackFrameIteratorText = $stackFrameIteratorText.Replace($iteratorInitialNeedle, $iteratorInitial.TrimEnd())
        $iteratorLookupNeedle = @'
        m_pCodeManager = dac_cast<PTR_ICodeManager>(m_pInstance->GetCodeManagerForAddress(m_ControlPC));
        FAILFAST_OR_DAC_FAIL(m_pCodeManager);

        FAILFAST_OR_DAC_FAIL(m_pCodeManager->FindMethodInfo(m_ControlPC, &m_methodInfo));
'@
        $iteratorLookupReplacement = @'
        m_pCodeManager = dac_cast<PTR_ICodeManager>(m_pInstance->GetCodeManagerForAddress(m_ControlPC));
        guideXosNativeAotC011EC18IteratorCodeManagerLookup(
            reinterpret_cast<uintptr_t>(m_ControlPC),
            static_cast<uintptr_t>(m_RegDisplay.GetSP()),
            static_cast<uintptr_t>(m_RegDisplay.GetFP()),
            reinterpret_cast<uintptr_t>(m_pCodeManager));
        FAILFAST_OR_DAC_FAIL(m_pCodeManager);

        const bool guideXosC011EC18FindMethodInfoResult =
            m_pCodeManager->FindMethodInfo(m_ControlPC, &m_methodInfo);
        guideXosNativeAotC011EC18IteratorFindMethodInfo(
            reinterpret_cast<uintptr_t>(m_ControlPC),
            reinterpret_cast<uintptr_t>(&m_methodInfo),
            guideXosC011EC18FindMethodInfoResult ? 1u : 0u);
        FAILFAST_OR_DAC_FAIL(guideXosC011EC18FindMethodInfoResult);
'@
        if (-not $stackFrameIteratorText.Contains($iteratorLookupNeedle)) {
            throw "C011EC18 StackFrameIterator code-manager injection point was not found."
        }
        $stackFrameIteratorText = $stackFrameIteratorText.Replace($iteratorLookupNeedle, $iteratorLookupReplacement.TrimEnd())
        $stackFrameIteratorText = $stackFrameIteratorText.Replace(
            '    m_FramePointer = GetCodeManager()->GetFramePointer(&m_methodInfo, &m_RegDisplay);',
            '    m_FramePointer = GetCodeManager()->GetFramePointer(&m_methodInfo, &m_RegDisplay);' + [Environment]::NewLine + '    guideXosNativeAotC011EC18IteratorFramePointer(reinterpret_cast<uintptr_t>(m_FramePointer));')
        $iteratorUnwindReplacement = '    NextInternal();' + [Environment]::NewLine + '    guideXosNativeAotC011EC18IteratorUnwind(reinterpret_cast<uintptr_t>(m_ControlPC), static_cast<uintptr_t>(m_RegDisplay.GetSP()));' + [Environment]::NewLine
        $iteratorUnwindPattern = '    NextInternal\(\);\r?\n(?=    STRESS_LOG1)'
        if ($stackFrameIteratorText -notmatch $iteratorUnwindPattern) {
            throw "C011EC18 StackFrameIterator unwind injection point was not found."
        }
        $stackFrameIteratorText = [regex]::Replace($stackFrameIteratorText, $iteratorUnwindPattern, $iteratorUnwindReplacement, 1)
        if ($stackFrameIteratorText -notmatch 'guideXosNativeAotC011EC18IteratorInitial' -or
            $stackFrameIteratorText -notmatch 'guideXosNativeAotC011EC18IteratorCodeManagerLookup' -or
            $stackFrameIteratorText -notmatch 'guideXosNativeAotC011EC18IteratorFindMethodInfo' -or
            $stackFrameIteratorText -notmatch 'guideXosNativeAotC011EC18IteratorFramePointer' -or
            $stackFrameIteratorText -notmatch 'guideXosNativeAotC011EC18IteratorUnwind' -or
            ($isC011EC23 -and $stackFrameIteratorText -notmatch 'guideXosNativeAotC011EC23TryNativeUnwind')) {
            throw "C011EC18 StackFrameIterator source injection did not match all required iterator boundaries."
        }
        Set-Content -LiteralPath $stackFrameIteratorSource -Value $stackFrameIteratorText -Encoding ASCII

        if ($isC011EC26) {
            $lockedThreadPath = Join-Path $lockedSourceRoot "src\coreclr\nativeaot\Runtime\thread.cpp"
            Require-File $lockedThreadPath "Locked NativeAOT thread.cpp source"
            $threadText = (Get-Content -LiteralPath $lockedThreadPath -Raw).Replace("`r`n", "`n")
            $threadDeclarations = @'
extern "C" void __cdecl guideXosNativeAotC011EC26ThreadGcScanRootsEntered();
extern "C" void __cdecl guideXosNativeAotC011EC26ThreadGcScanRootsReturned();
extern "C" void __cdecl guideXosNativeAotC011EC26PostStackRootSource(uint32_t source);
'@
            $threadIncludeNeedle = '#include "thread.h"'
            if (-not $threadText.Contains($threadIncludeNeedle)) {
                throw "C011EC26 thread.cpp include injection point was not found."
            }
            $threadText = $threadText.Replace(
                $threadIncludeNeedle,
                $threadIncludeNeedle + [Environment]::NewLine + $threadDeclarations.TrimEnd())
            $threadReversePInvokeNeedle = @'
FCIMPL1(void, RhpReversePInvoke, ReversePInvokeFrame * pFrame)
{
    Thread * pCurThread = ThreadStore::RawGetCurrentThread();
    pFrame->m_savedThread = pCurThread;
    if (pCurThread->InlineTryFastReversePInvoke(pFrame))
        return;

    RhpReversePInvokeAttachOrTrapThread2(pFrame);
}
FCIMPLEND

FCIMPL1(void, RhpReversePInvokeReturn, ReversePInvokeFrame * pFrame)
{
    pFrame->m_savedThread->InlineReversePInvokeReturn(pFrame);
}
FCIMPLEND
'@
            if (-not $threadText.Contains($threadReversePInvokeNeedle)) {
                throw "C011EC26 thread.cpp reverse-P/Invoke definitions were not found."
            }
            $threadReversePInvokeReplacement = @'
#if !defined(GUIDEXOS_NATIVEAOT_C011EC26_PLATFORM_RHP_REVERSE_PINVOKE)
FCIMPL1(void, RhpReversePInvoke, ReversePInvokeFrame * pFrame)
{
    Thread * pCurThread = ThreadStore::RawGetCurrentThread();
    pFrame->m_savedThread = pCurThread;
    if (pCurThread->InlineTryFastReversePInvoke(pFrame))
        return;

    RhpReversePInvokeAttachOrTrapThread2(pFrame);
}
FCIMPLEND

FCIMPL1(void, RhpReversePInvokeReturn, ReversePInvokeFrame * pFrame)
{
    pFrame->m_savedThread->InlineReversePInvokeReturn(pFrame);
}
FCIMPLEND
#endif
'@
            $threadText = $threadText.Replace($threadReversePInvokeNeedle, $threadReversePInvokeReplacement.TrimEnd())
            $threadEntryNeedle = 'void Thread::GcScanRoots(ScanFunc * pfnEnumCallback, ScanContext * pvCallbackData)' + "`n" + '{'
            if (-not $threadText.Contains($threadEntryNeedle)) {
                throw "C011EC26 Thread::GcScanRoots entry was not found."
            }
            $threadText = $threadText.Replace(
                $threadEntryNeedle,
                $threadEntryNeedle + "`n" + '    guideXosNativeAotC011EC26ThreadGcScanRootsEntered();')
            $threadWorkerNeedle = '    GcScanRootsWorker(pfnEnumCallback, pvCallbackData, frameIterator);'
            if (-not $threadText.Contains($threadWorkerNeedle)) {
                throw "C011EC26 Thread::GcScanRoots worker return point was not found."
            }
            $threadText = Replace-First $threadText $threadWorkerNeedle (
                $threadWorkerNeedle + "`n" +
                '    guideXosNativeAotC011EC26ThreadGcScanRootsReturned();')
            $threadLoopNeedle = '            frameIterator.CalculateCurrentMethodState();' + "`n`n" + '            STRESS_LOG1'
            if (-not $threadText.Contains($threadLoopNeedle)) {
                throw "C011EC26 GcScanRootsWorker iterator completion guard point was not found."
            }
            $threadText = $threadText.Replace(
                $threadLoopNeedle,
                 '            frameIterator.CalculateCurrentMethodState();' + "`n`n" +
                 '            if (!frameIterator.IsValid())' + "`n" +
                 '            {' + "`n" +
                 '                break;' + "`n" +
                 '            }' + "`n`n" +
                 '            STRESS_LOG1')
            $threadPostStackExceptionNeedle = '        EnumGcRef(pExceptionObj, GCRK_Object, pfnEnumCallback, pvCallbackData);'
            if (-not $threadText.Contains($threadPostStackExceptionNeedle)) {
                throw "C011EC26 post-stack exception-root point was not found."
            }
            $threadText = $threadText.Replace(
                $threadPostStackExceptionNeedle,
                '        guideXosNativeAotC011EC26PostStackRootSource(1u);' + "`n" + $threadPostStackExceptionNeedle)
            $threadPostStackFrameNeedle = '            EnumGcRef(dac_cast<PTR_OBJECTREF>(pCurGCFrame->m_pObjRefs + i),'
            if (-not $threadText.Contains($threadPostStackFrameNeedle)) {
                throw "C011EC26 post-stack GC-frame root point was not found."
            }
            $threadText = $threadText.Replace(
                $threadPostStackFrameNeedle,
                '            guideXosNativeAotC011EC26PostStackRootSource(2u);' + "`n" + $threadPostStackFrameNeedle)
            $threadPostStackAbortNeedle = '    EnumGcRef(pThreadAbortExceptionObj, GCRK_Object, pfnEnumCallback, pvCallbackData);'
            if (-not $threadText.Contains($threadPostStackAbortNeedle)) {
                throw "C011EC26 post-stack ThreadAbort root point was not found."
            }
            $threadText = $threadText.Replace(
                $threadPostStackAbortNeedle,
                '    guideXosNativeAotC011EC26PostStackRootSource(3u);' + "`n" + $threadPostStackAbortNeedle)
            if ($threadText -notmatch 'guideXosNativeAotC011EC26ThreadGcScanRootsEntered' -or
                $threadText -notmatch 'guideXosNativeAotC011EC26ThreadGcScanRootsReturned' -or
                $threadText -notmatch 'if \(!frameIterator\.IsValid\(\)\)' -or
                $threadText -notmatch 'guideXosNativeAotC011EC26PostStackRootSource\(1u\)' -or
                $threadText -notmatch 'guideXosNativeAotC011EC26PostStackRootSource\(2u\)' -or
                $threadText -notmatch 'guideXosNativeAotC011EC26PostStackRootSource\(3u\)') {
                throw "C011EC26 thread.cpp injection did not match all required completion boundaries."
            }
            Set-Content -LiteralPath $threadC011EC26Source -Value $threadText -Encoding ASCII
        }

        Copy-Item -LiteralPath $allocFastObj -Destination $allocFastPublicObj -Force
        Invoke-LoggedCommand $objcopy @('--redefine-sym', 'guideXosStockRhpNewArray=RhpNewArray', $allocFastPublicObj) (Join-Path $runRoot 'allocfast-public-symbol.log')

        if ($isC011EC19) {
            $lockedCoffPath = Join-Path $lockedSourceRoot "src\coreclr\nativeaot\Runtime\windows\CoffNativeCodeManager.cpp"
            $lockedGcInfoWrapperPath = Join-Path $root "out\dotnet\gc-feasibility-baseline\nativeaot-runtime\src\coreclr\nativeaot\Runtime\gcinfodecoder.cpp"
            Require-File $lockedCoffPath "Locked NativeAOT CoffNativeCodeManager.cpp source"
            Require-File $lockedGcInfoWrapperPath "Locked NativeAOT GC-info decoder wrapper"
            $coffText = (Get-Content -LiteralPath $lockedCoffPath -Raw).Replace([string]([char]13) + [string]([char]10), [string][char]10)
            $coffDeclarations = @'
extern "C" void __cdecl guideXosNativeAotC011EC19UnwindEntered(uintptr_t methodInfo, uintptr_t controlPc, uintptr_t sp, uintptr_t fp, uintptr_t runtimeFunction, uintptr_t mainRuntimeFunction);
extern "C" void __cdecl guideXosNativeAotC011EC19UnwindMetadata(uintptr_t unwindInfo, uintptr_t unwindInfoSize, uintptr_t blockFlags, uintptr_t methodStart, uintptr_t methodEnd, uintptr_t ehInfo);
extern "C" void __cdecl guideXosNativeAotC011EC19UnwindCompleted(uint32_t result, uint32_t rtlVirtualUnwindCalled, uintptr_t callerPc, uintptr_t callerSp, uintptr_t callerFp, uintptr_t previousTransitionFrame, uint32_t preservedRegisters);
extern "C" void __cdecl guideXosNativeAotC011EC19GcInfoLookup(uintptr_t methodInfo, uintptr_t safePoint, uintptr_t gcInfo, uintptr_t codeOffset, uintptr_t unwindInfo, uintptr_t unwindInfoSize, uintptr_t blockFlags, uintptr_t methodStart, uintptr_t methodEnd);
extern "C" void __cdecl guideXosNativeAotC011EC19GcInfoDecodeStarted(uintptr_t gcInfo, uintptr_t codeOffset, uint32_t activeFrame);
extern "C" void __cdecl guideXosNativeAotC011EC19GcInfoInterruptibility(uint32_t interruptible, uint32_t hasRanges);
extern "C" void __cdecl guideXosNativeAotC011EC19GcInfoDecodeCompleted(uint32_t result);
extern "C" __declspec(noreturn) void __cdecl guideXosNativeAotC011EC19SafeStop(uint32_t reason);
extern "C" void __cdecl guideXosNativeAotC011EC20TransitionCrossed(uintptr_t frameType, uintptr_t frameAddress, uintptr_t savedRip, uintptr_t savedSp, uintptr_t savedFp, uintptr_t threadAddress, uintptr_t flags, uintptr_t previousTransitionFrame);
extern "C" void __cdecl guideXosNativeAotC011EC20UnwindInputs(uintptr_t imageBase, uintptr_t runtimeFunction, uintptr_t beginRva, uintptr_t endRva, uintptr_t unwindInfo, uintptr_t unwindInfoSize, uintptr_t blockFlags, uintptr_t inputRip, uintptr_t inputRsp, uintptr_t inputRbp, uintptr_t inputRbx, uintptr_t inputRsi, uintptr_t inputRdi, uintptr_t inputR12, uintptr_t inputR13, uintptr_t inputR14, uintptr_t inputR15);
extern "C" void __cdecl guideXosNativeAotC011EC20UnwindCompleted(uint32_t result, uintptr_t outputRip, uintptr_t outputRsp, uintptr_t outputRbp, uintptr_t establisherFrame, uintptr_t handlerData, uintptr_t rtlVirtualUnwindResult, uintptr_t restoredRbx, uintptr_t restoredRsi, uintptr_t restoredRdi, uintptr_t restoredR12, uintptr_t restoredR13, uintptr_t restoredR14, uintptr_t restoredR15, uint32_t restoredRegisterCount, uintptr_t previousTransitionFrame);
extern "C" void __cdecl guideXosNativeAotC011EC20CallerMethodInfo(uintptr_t controlPc, uintptr_t codeManager, uintptr_t methodInfo, uintptr_t methodStart, uintptr_t methodEnd, uintptr_t runtimeFunction, uintptr_t unwindInfo, uintptr_t unwindInfoSize, uintptr_t blockFlags);
extern "C" __declspec(noreturn) void __cdecl guideXosNativeAotC011EC20SafeStop(uint32_t reason);
'@
            $coffText = $coffText.Replace(
                '#include "CoffNativeCodeManager.h"',
                '#include "CoffNativeCodeManager.h"' + [Environment]::NewLine + $coffDeclarations.TrimEnd())
            $coffText = $coffText.Replace(
                '#include "gcinfodecoder.cpp"',
                '#include "' + $lockedGcInfoWrapperPath + '"')
            $coffCallerMethodNeedle = @'
    pMethodInfo->executionAborted = false;

    return true;
'@
            $coffCallerMethodReplacement = @'
    pMethodInfo->executionAborted = false;

#if defined(GUIDEXOS_NATIVEAOT_C011EC20_UNWIND)
    size_t guideXosCallerUnwindDataBlobSize;
    PTR_VOID guideXosCallerUnwindDataBlob = GetUnwindDataBlob(
        m_moduleBase, pMethodInfo->runtimeFunction, &guideXosCallerUnwindDataBlobSize);
    uint8_t guideXosCallerBlockFlags = *(
        dac_cast<DPTR(uint8_t)>(guideXosCallerUnwindDataBlob) +
        guideXosCallerUnwindDataBlobSize);
    guideXosNativeAotC011EC20CallerMethodInfo(
        reinterpret_cast<uintptr_t>(ControlPC), reinterpret_cast<uintptr_t>(this),
        reinterpret_cast<uintptr_t>(pMethodInfoOut),
        static_cast<uintptr_t>(m_moduleBase + pMethodInfo->mainRuntimeFunction->BeginAddress),
        static_cast<uintptr_t>(m_moduleBase + pMethodInfo->mainRuntimeFunction->EndAddress),
        reinterpret_cast<uintptr_t>(pMethodInfo->runtimeFunction),
        reinterpret_cast<uintptr_t>(guideXosCallerUnwindDataBlob),
        guideXosCallerUnwindDataBlobSize,
        static_cast<uintptr_t>(guideXosCallerBlockFlags));
#endif

    return true;
'@
            if (-not $coffText.Contains($coffCallerMethodNeedle)) { throw "C011EC20 CoffNativeCodeManager caller FindMethodInfo boundary was not found." }
            $coffText = Replace-First $coffText $coffCallerMethodNeedle $coffCallerMethodReplacement.TrimEnd()
            $coffGcNeedle = @'
    *gcInfo = p;

    TADDR methodStartAddress = m_moduleBase + pNativeMethodInfo->mainRuntimeFunction->BeginAddress;
    return (uint32_t)(dac_cast<TADDR>(address) - methodStartAddress);
'@
            $coffGcReplacement = @'
    *gcInfo = p;

    TADDR methodStartAddress = m_moduleBase + pNativeMethodInfo->mainRuntimeFunction->BeginAddress;
    uint32_t guideXosCodeOffset = (uint32_t)(dac_cast<TADDR>(address) - methodStartAddress);
    guideXosNativeAotC011EC19GcInfoLookup(
        reinterpret_cast<uintptr_t>(pMethodInfo), reinterpret_cast<uintptr_t>(address),
        reinterpret_cast<uintptr_t>(p), static_cast<uintptr_t>(guideXosCodeOffset),
        reinterpret_cast<uintptr_t>(pUnwindDataBlob), unwindDataBlobSize,
        static_cast<uintptr_t>(unwindBlockFlags),
        static_cast<uintptr_t>(methodStartAddress),
        static_cast<uintptr_t>(methodStartAddress +
            (pNativeMethodInfo->mainRuntimeFunction->EndAddress -
             pNativeMethodInfo->mainRuntimeFunction->BeginAddress)));
    return guideXosCodeOffset;
'@
            if (-not $coffText.Contains($coffGcNeedle)) { throw "C011EC19 CoffNativeCodeManager GetCodeOffset boundary was not found." }
            $coffText = Replace-First $coffText $coffGcNeedle $coffGcReplacement.TrimEnd()
            $coffEnumNeedle = @'
#ifdef USE_GC_INFO_DECODER
    if (!isActiveStackFrame && !executionAborted)
'@
            $coffEnumReplacement = @'
#ifdef USE_GC_INFO_DECODER
    guideXosNativeAotC011EC19GcInfoDecodeStarted(
        reinterpret_cast<uintptr_t>(gcInfo), static_cast<uintptr_t>(codeOffset),
        isActiveStackFrame ? 1u : 0u);
    GcInfoDecoder guideXosInterruptibilityDecoder(
        GCInfoToken(gcInfo), GcInfoDecoderFlags(DECODE_INTERRUPTIBILITY), codeOffset);
    guideXosNativeAotC011EC19GcInfoInterruptibility(
        guideXosInterruptibilityDecoder.IsInterruptible() ? 1u : 0u,
        guideXosInterruptibilityDecoder.HasInterruptibleRanges() ? 1u : 0u);
    if (!isActiveStackFrame && !executionAborted)
'@
            if (-not $coffText.Contains($coffEnumNeedle)) { throw "C011EC19 CoffNativeCodeManager EnumGcRefs decoder boundary was not found." }
            $coffText = Replace-First $coffText $coffEnumNeedle $coffEnumReplacement.TrimEnd()
            $coffDecodeNeedle = @'
    if (!decoder.EnumerateLiveSlots(
        pRegisterSet,
        isActiveStackFrame /* reportScratchSlots */,
        flags,
        hCallback->pCallback,
        hCallback
        ))
    {
        assert(false);
    }
'@
            $coffDecodeReplacement = @'
    bool guideXosDecodeResult = decoder.EnumerateLiveSlots(
        pRegisterSet,
        isActiveStackFrame /* reportScratchSlots */,
        flags,
        hCallback->pCallback,
        hCallback);
    guideXosNativeAotC011EC19GcInfoDecodeCompleted(
        guideXosDecodeResult ? 1u : 0u);
    if (!guideXosDecodeResult)
    {
        assert(false);
    }
'@
            if (-not $coffText.Contains($coffDecodeNeedle)) { throw "C011EC19 CoffNativeCodeManager live-slot decode boundary was not found." }
            $coffText = Replace-First $coffText $coffDecodeNeedle $coffDecodeReplacement.TrimEnd()
            $coffUnwindNeedle = @'
    CoffNativeMethodInfo * pNativeMethodInfo = (CoffNativeMethodInfo *)pMethodInfo;

    size_t unwindDataBlobSize;
'@
            $coffUnwindReplacement = @'
    CoffNativeMethodInfo * pNativeMethodInfo = (CoffNativeMethodInfo *)pMethodInfo;

    guideXosNativeAotC011EC19UnwindEntered(
        reinterpret_cast<uintptr_t>(pMethodInfo),
        static_cast<uintptr_t>(pRegisterSet->IP),
        static_cast<uintptr_t>(pRegisterSet->GetSP()),
        static_cast<uintptr_t>(pRegisterSet->GetFP()),
        reinterpret_cast<uintptr_t>(pNativeMethodInfo->runtimeFunction),
        reinterpret_cast<uintptr_t>(pNativeMethodInfo->mainRuntimeFunction));

    size_t unwindDataBlobSize;
'@
            if (-not $coffText.Contains($coffUnwindNeedle)) { throw "C011EC19 CoffNativeCodeManager unwind entry boundary was not found." }
            $coffText = Replace-First $coffText $coffUnwindNeedle $coffUnwindReplacement.TrimEnd()
            $coffUnwindMetaNeedle = @'
    uint8_t unwindBlockFlags = *p++;

    if ((unwindBlockFlags & UBF_FUNC_HAS_ASSOCIATED_DATA) != 0)
'@
            $coffUnwindMetaReplacement = @'
    uint8_t unwindBlockFlags = *p++;

    guideXosNativeAotC011EC19UnwindMetadata(
        reinterpret_cast<uintptr_t>(pUnwindDataBlob), unwindDataBlobSize,
        static_cast<uintptr_t>(unwindBlockFlags),
        static_cast<uintptr_t>(m_moduleBase + pNativeMethodInfo->mainRuntimeFunction->BeginAddress),
        static_cast<uintptr_t>(m_moduleBase + pNativeMethodInfo->mainRuntimeFunction->EndAddress),
        (unwindBlockFlags & UBF_FUNC_HAS_EHINFO) != 0u
            ? reinterpret_cast<uintptr_t>(p) : 0u);

    if ((unwindBlockFlags & UBF_FUNC_HAS_ASSOCIATED_DATA) != 0)
'@
            if (-not $coffText.Contains($coffUnwindMetaNeedle)) { throw "C011EC19 CoffNativeCodeManager unwind metadata boundary was not found." }
            $coffText = Replace-First $coffText $coffUnwindMetaNeedle $coffUnwindMetaReplacement.TrimEnd()
            $coffTransitionStopNeedle = @'
        if ((flags & USFF_StopUnwindOnTransitionFrame) != 0)
        {
            return true;
        }
'@
            $coffTransitionStopReplacement = if ($isC011EC20) {
@'
        if ((flags & USFF_StopUnwindOnTransitionFrame) != 0)
        {
            PInvokeTransitionFrame* guideXosTransitionFrame = *ppPreviousTransitionFrame;
            uintptr_t guideXosTransitionFlags = guideXosTransitionFrame != nullptr
                ? static_cast<uintptr_t>(guideXosTransitionFrame->m_Flags) : 0u;
            uintptr_t guideXosTransitionSavedSp = 0u;
            if (guideXosTransitionFrame != nullptr &&
                (guideXosTransitionFlags & PTFF_SAVE_RSP) != 0u)
            {
                const uintptr_t* guideXosSavedRegs = reinterpret_cast<const uintptr_t*>(
                    guideXosTransitionFrame->m_PreservedRegs);
                guideXosTransitionSavedSp = guideXosSavedRegs[7];
            }
            guideXosNativeAotC011EC20TransitionCrossed(
                1u, reinterpret_cast<uintptr_t>(guideXosTransitionFrame),
                guideXosTransitionFrame != nullptr
                    ? reinterpret_cast<uintptr_t>(guideXosTransitionFrame->m_RIP) : 0u,
                guideXosTransitionSavedSp,
                guideXosTransitionFrame != nullptr
                    ? reinterpret_cast<uintptr_t>(guideXosTransitionFrame->m_FramePointer) : 0u,
                guideXosTransitionFrame != nullptr
                    ? reinterpret_cast<uintptr_t>(guideXosTransitionFrame->m_pThread) : 0u,
                guideXosTransitionFlags,
                reinterpret_cast<uintptr_t>(*ppPreviousTransitionFrame));
            // A null value is the legitimate top-level reverse-P/Invoke
            // result: this slot contains the previous transition frame, not
            // the current frame independently proven by C011EC19.  C20
            // preserves that boundary and continues into the locked ordinary
            // AMD64 unwind below; it never synthesizes a caller frame.
        }
'@.TrimEnd()
            } else {
@'
        if ((flags & USFF_StopUnwindOnTransitionFrame) != 0)
        {
            guideXosNativeAotC011EC19UnwindCompleted(
                1u, 0u, static_cast<uintptr_t>(pRegisterSet->IP),
                static_cast<uintptr_t>(pRegisterSet->GetSP()),
                static_cast<uintptr_t>(pRegisterSet->GetFP()),
                reinterpret_cast<uintptr_t>(*ppPreviousTransitionFrame), 0u);
            guideXosNativeAotC011EC19SafeStop(0xC0190002u);
            return true;
        }
'@.TrimEnd()
            }
            if (-not $coffText.Contains($coffTransitionStopNeedle)) { throw "C011EC19 CoffNativeCodeManager transition-stop unwind boundary was not found." }
            $coffText = Replace-First $coffText $coffTransitionStopNeedle $coffTransitionStopReplacement.TrimEnd()
            $coffRtlNeedle = @'
    RtlVirtualUnwind(NULL,
                    dac_cast<TADDR>(m_moduleBase),
                    pRegisterSet->IP,
                    (PRUNTIME_FUNCTION)pNativeMethodInfo->runtimeFunction,
                    &context,
                    &HandlerData,
                    &EstablisherFrame,
                    &contextPointers);

    pRegisterSet->SP = context.Rsp;
'@
            $coffRtlReplacement = if ($isC011EC20) {
@'
#if defined(TARGET_AMD64)
    guideXosNativeAotC011EC20UnwindInputs(
        static_cast<uintptr_t>(m_moduleBase),
        reinterpret_cast<uintptr_t>(pNativeMethodInfo->runtimeFunction),
        static_cast<uintptr_t>(pNativeMethodInfo->runtimeFunction->BeginAddress),
        static_cast<uintptr_t>(pNativeMethodInfo->runtimeFunction->EndAddress),
        reinterpret_cast<uintptr_t>(pUnwindDataBlob), unwindDataBlobSize,
        static_cast<uintptr_t>(unwindBlockFlags),
        static_cast<uintptr_t>(context.Rip), static_cast<uintptr_t>(context.Rsp),
        static_cast<uintptr_t>(context.Rbp), static_cast<uintptr_t>(context.Rbx),
        static_cast<uintptr_t>(context.Rsi), static_cast<uintptr_t>(context.Rdi),
        static_cast<uintptr_t>(context.R12), static_cast<uintptr_t>(context.R13),
        static_cast<uintptr_t>(context.R14), static_cast<uintptr_t>(context.R15));
#endif

    PEXCEPTION_ROUTINE guideXosRtlVirtualUnwindResult = RtlVirtualUnwind(NULL,
                    dac_cast<TADDR>(m_moduleBase),
                    pRegisterSet->IP,
                    (PRUNTIME_FUNCTION)pNativeMethodInfo->runtimeFunction,
                    &context,
                    &HandlerData,
                    &EstablisherFrame,
                    &contextPointers);

    pRegisterSet->SP = context.Rsp;
    pRegisterSet->IP = context.Rip;

#if defined(TARGET_AMD64)
    guideXosNativeAotC011EC20UnwindCompleted(
        1u, static_cast<uintptr_t>(context.Rip), static_cast<uintptr_t>(context.Rsp),
        static_cast<uintptr_t>(context.Rbp), static_cast<uintptr_t>(EstablisherFrame),
        reinterpret_cast<uintptr_t>(HandlerData),
        reinterpret_cast<uintptr_t>(guideXosRtlVirtualUnwindResult),
        static_cast<uintptr_t>(context.Rbx),
        static_cast<uintptr_t>(context.Rsi), static_cast<uintptr_t>(context.Rdi),
        static_cast<uintptr_t>(context.R12), static_cast<uintptr_t>(context.R13),
        static_cast<uintptr_t>(context.R14), static_cast<uintptr_t>(context.R15),
        (contextPointers.Rbx != nullptr ? 1u : 0u) +
        (contextPointers.Rsi != nullptr ? 1u : 0u) +
        (contextPointers.Rdi != nullptr ? 1u : 0u) +
        (contextPointers.R12 != nullptr ? 1u : 0u) +
        (contextPointers.R13 != nullptr ? 1u : 0u) +
        (contextPointers.R14 != nullptr ? 1u : 0u) +
        (contextPointers.R15 != nullptr ? 1u : 0u),
        reinterpret_cast<uintptr_t>(*ppPreviousTransitionFrame));
#endif
'@.TrimEnd()
            } else {
@'
    RtlVirtualUnwind(NULL,
                    dac_cast<TADDR>(m_moduleBase),
                    pRegisterSet->IP,
                    (PRUNTIME_FUNCTION)pNativeMethodInfo->runtimeFunction,
                    &context,
                    &HandlerData,
                    &EstablisherFrame,
                    &contextPointers);

    guideXosNativeAotC011EC19UnwindCompleted(
        1u, 1u, static_cast<uintptr_t>(context.Rip),
        static_cast<uintptr_t>(context.Rsp),
        static_cast<uintptr_t>(pRegisterSet->GetFP()),
        reinterpret_cast<uintptr_t>(*ppPreviousTransitionFrame), 8u);
    guideXosNativeAotC011EC19SafeStop(0xC0190001u);

    pRegisterSet->SP = context.Rsp;
'@
            }
            if (-not $coffText.Contains($coffRtlNeedle)) { throw "C011EC19 CoffNativeCodeManager RtlVirtualUnwind boundary was not found." }
            $coffText = Replace-First $coffText $coffRtlNeedle $coffRtlReplacement.TrimEnd()
            Set-Content -LiteralPath $coffNativeCodeManagerSource -Value $coffText -Encoding ASCII
        }
    }

    if ($isFirstRootCallbackEntry) {
        $lockedGcwksPath = Join-Path $lockedSourceRoot "src\coreclr\gc\gcwks.cpp"
        $lockedGcCppPath = Join-Path $lockedSourceRoot "src\coreclr\gc\gc.cpp"
        $lockedGcPrivPath = Join-Path $lockedSourceRoot "src\coreclr\gc\gcpriv.h"
        Require-File $lockedGcwksPath "Locked NativeAOT gcwks.cpp source"
        Require-File $lockedGcCppPath "Locked Workstation GC gc.cpp source"
        Require-File $lockedGcPrivPath "Locked Workstation GC gcpriv.h source"
        $gcWksText = (Get-Content -LiteralPath $lockedGcwksPath -Raw).Replace([string]([char]13) + [string]([char]10), [string][char]10)
        $gcCppText = (Get-Content -LiteralPath $lockedGcCppPath -Raw).Replace([string]([char]13) + [string]([char]10), [string][char]10)
        if ($isC011EC34) {
            $gcCppDeclaration = @'
extern "C" void __cdecl guideXosNativeAotC011EC34RelocateEntered(uintptr_t slot, uintptr_t oldObject, uintptr_t scanContext, uint32_t flags);
extern "C" void __cdecl guideXosNativeAotC011EC34RelocateReturned(uintptr_t slot, uintptr_t oldObject, uintptr_t newObject);
extern "C" void __cdecl guideXosNativeAotC011EC34RelocationLookupEntered(uintptr_t oldObject, uintptr_t brickTable, uintptr_t brickIndex, uintptr_t brickEntry);
extern "C" void __cdecl guideXosNativeAotC011EC34RelocationLookupObserved(uintptr_t oldObject, uintptr_t newObject, uintptr_t brickTable, uintptr_t brickIndex, uintptr_t brickEntry, uintptr_t treeNode, uintptr_t relocationDistance, uint32_t success);
'@
            $gcCppText = $gcCppText.Replace('#include "gcpriv.h"', '#include "gcpriv.h"' + [Environment]::NewLine + [Environment]::NewLine + $gcCppDeclaration.TrimEnd())
            $relocateStart = $gcCppText.IndexOf('void GCHeap::Relocate (Object** ppObject, ScanContext* sc,')
            $relocateEnd = $gcCppText.IndexOf('/*static*/ bool GCHeap::IsLargeObject', $relocateStart)
            if ($relocateStart -lt 0 -or $relocateEnd -le $relocateStart) {
                throw "C011EC34 could not isolate locked GCHeap::Relocate."
            }
            $relocateFunction = $gcCppText.Substring($relocateStart, $relocateEnd - $relocateStart)
            $relocateFunction = $relocateFunction.Replace(
                '    uint8_t* object = (uint8_t*)(Object*)(*ppObject);',
                '    uint8_t* object = (uint8_t*)(Object*)(*ppObject);' + [Environment]::NewLine + '    guideXosNativeAotC011EC34RelocateEntered(reinterpret_cast<uintptr_t>(ppObject), reinterpret_cast<uintptr_t>(object), reinterpret_cast<uintptr_t>(sc), flags);')
            $relocateFunction = $relocateFunction.Replace(
                '        return;',
                '        guideXosNativeAotC011EC34RelocateReturned(reinterpret_cast<uintptr_t>(ppObject), reinterpret_cast<uintptr_t>(object), reinterpret_cast<uintptr_t>(*ppObject));' + [Environment]::NewLine + '        return;')
            $relocateFunction = $relocateFunction.Replace(
                '    int    brick_entry =  brick_table [ brick ];',
                '    int    brick_entry =  brick_table [ brick ];' + [Environment]::NewLine + '    guideXosNativeAotC011EC34RelocationLookupEntered(reinterpret_cast<uintptr_t>(old_address), reinterpret_cast<uintptr_t>(brick_table), static_cast<uintptr_t>(brick), static_cast<uintptr_t>(brick_entry));')
            $relocateFunction = $relocateFunction.Replace(
                '    STRESS_LOG_ROOT_RELOCATE(ppObject, object, pheader, ((!(flags & GC_CALL_INTERIOR)) ? ((Object*)object)->GetGCSafeMethodTable() : 0));',
                '    STRESS_LOG_ROOT_RELOCATE(ppObject, object, pheader, ((!(flags & GC_CALL_INTERIOR)) ? ((Object*)object)->GetGCSafeMethodTable() : 0));' + [Environment]::NewLine + '    guideXosNativeAotC011EC34RelocateReturned(reinterpret_cast<uintptr_t>(ppObject), reinterpret_cast<uintptr_t>(object), reinterpret_cast<uintptr_t>(*ppObject));')
            $gcCppText = $gcCppText.Substring(0, $relocateStart) + $relocateFunction + $gcCppText.Substring($relocateEnd)
            $relocateLookupStart = $gcCppText.IndexOf('void gc_heap::relocate_address (uint8_t** pold_address THREAD_NUMBER_DCL)')
            $relocateLookupEnd = $gcCppText.IndexOf('inline void' + [string][char]10 + 'gc_heap::check_class_object_demotion (uint8_t* obj)', $relocateLookupStart)
            if ($relocateLookupStart -lt 0 -or $relocateLookupEnd -le $relocateLookupStart) {
                throw "C011EC34 could not isolate locked gc_heap::relocate_address."
            }
            $relocateLookupFunction = $gcCppText.Substring($relocateLookupStart, $relocateLookupEnd - $relocateLookupStart)
            $relocateLookupFunction = Replace-First $relocateLookupFunction '    int    brick_entry =  brick_table [ brick ];' (
                '    int    brick_entry =  brick_table [ brick ];' + [Environment]::NewLine + '    guideXosNativeAotC011EC34RelocationLookupEntered(reinterpret_cast<uintptr_t>(old_address), reinterpret_cast<uintptr_t>(brick_table), static_cast<uintptr_t>(brick), static_cast<uintptr_t>(brick_entry));')
            $relocateLookupFunction = Replace-First $relocateLookupFunction '        *pold_address = new_address;' (
                '        guideXosNativeAotC011EC34RelocationLookupObserved(reinterpret_cast<uintptr_t>(old_address), reinterpret_cast<uintptr_t>(new_address), reinterpret_cast<uintptr_t>(brick_table), static_cast<uintptr_t>(brick), static_cast<uintptr_t>(brick_entry), 0u, 0u, 1u);' + [Environment]::NewLine +
                '        *pold_address = new_address;')
            $relocateLookupFunction = Replace-First $relocateLookupFunction '#ifdef FEATURE_LOH_COMPACTION' (
                '    if (brick_entry == 0) {' + [Environment]::NewLine + '        guideXosNativeAotC011EC34RelocationLookupObserved(reinterpret_cast<uintptr_t>(old_address), reinterpret_cast<uintptr_t>(new_address), reinterpret_cast<uintptr_t>(brick_table), static_cast<uintptr_t>(brick), static_cast<uintptr_t>(brick_entry), 0u, 0u, 0u);' + [Environment]::NewLine + '    }' + [Environment]::NewLine + [Environment]::NewLine + '#ifdef FEATURE_LOH_COMPACTION')
            $gcCppText = $gcCppText.Substring(0, $relocateLookupStart) + $relocateLookupFunction + $gcCppText.Substring($relocateLookupEnd)
            if ($relocateFunction -notmatch 'guideXosNativeAotC011EC34RelocateEntered' -or
                $relocateFunction -notmatch 'guideXosNativeAotC011EC34RelocateReturned' -or
                $relocateLookupFunction -notmatch 'guideXosNativeAotC011EC34RelocationLookupEntered' -or
                $relocateLookupFunction -notmatch 'guideXosNativeAotC011EC34RelocationLookupObserved') {
                throw "C011EC34 relocation instrumentation did not match all locked boundaries: entered=$($relocateFunction -match 'guideXosNativeAotC011EC34RelocateEntered') returned=$($relocateFunction -match 'guideXosNativeAotC011EC34RelocateReturned') lookupEntry=$($relocateLookupFunction -match 'guideXosNativeAotC011EC34RelocationLookupEntered') lookupObserved=$($relocateLookupFunction -match 'guideXosNativeAotC011EC34RelocationLookupObserved')"
            }
            Set-Content -LiteralPath $gcCppSource -Value $gcCppText -Encoding ASCII
        }
        if ($isC011EC28) {
            $gcPrivText = (Get-Content -LiteralPath $lockedGcPrivPath -Raw).Replace([string]([char]13) + [string]([char]10), [string][char]10)
            foreach ($requiredQueueText in @(
                'class mark_queue_t',
                'static const size_t slot_count = 16;',
                'uint8_t* slot_table[slot_count];',
                'size_t curr_slot_index;',
                'uint8_t *queue_mark(uint8_t *o);',
                'uint8_t* get_next_marked();',
                'void verify_empty();')) {
                if (-not $gcPrivText.Contains($requiredQueueText)) {
                    throw "Locked mark_queue_t declaration is missing required text: $requiredQueueText"
                }
            }
            foreach ($requiredQueueText in @(
                'mark_queue_t::mark_queue_t()',
                'uint8_t *mark_queue_t::queue_mark(uint8_t *o)',
                'uint8_t* mark_queue_t::get_next_marked()',
                'void mark_queue_t::verify_empty()',
                'void gc_heap::drain_mark_queue ()',
                'while ((o = mark_queue.get_next_marked()) != nullptr)')) {
                if (-not $gcCppText.Contains($requiredQueueText)) {
                    throw "Locked mark queue implementation is missing required text: $requiredQueueText"
                }
            }
            Set-Content -LiteralPath (Join-Path $runRoot 'locked-queue-semantics.txt') -Value @(
                'declaration=src/coreclr/gc/gcpriv.h:1487-1504',
                'fields=slot_table[16] and curr_slot_index only; no stored head/tail/count',
                'constructor=src/coreclr/gc/gc.cpp:27290-27301 clears all slots and sets curr_slot_index=0',
                'insertion=src/coreclr/gc/gc.cpp:27303-27335 writes slot_table[curr_slot_index], advances modulo 16, returns displaced newly-marked object',
                'removal=src/coreclr/gc/gc.cpp:27373-27402 scans at most 16 slots from curr_slot_index, clears each slot, advances modulo 16, returns newly-marked object or nullptr',
                'drain=src/coreclr/gc/gc.cpp:28054-28090 loops while get_next_marked() is non-null',
                'phase=src/coreclr/gc/gc.cpp:29899-30089 root scan, drains, later mark sources, then AfterGcScanRoots',
                'configuration=Workstation single heap; MULTIPLE_HEAPS, BACKGROUND_GC, and MH_SC_MARK are not selected by the proof compile') -Encoding ASCII
        }
        $gcWksText = '#include <intrin.h>' + [Environment]::NewLine + $gcWksText
        $gcWksText = $gcWksText.Replace('namespace WKS {', '#define _DEBUG 1' + [Environment]::NewLine + 'namespace WKS {')
        $membershipDeclaration = ""
        if ($isFirstRootMembershipClassification -or $isFirstRootHeapResolutionOrCondemned) {
            $membershipDeclaration = @'
extern "C" void __cdecl guideXosNativeAotFirstRootMembershipCheckRequested(uintptr_t object);
extern "C" void __cdecl guideXosNativeAotFirstRootMembershipCheckEntered(uintptr_t object);
extern "C" void __cdecl guideXosNativeAotFirstRootMembershipCheckCompleted(uintptr_t object, uintptr_t lowerBound, uintptr_t upperBound, uint32_t lowerEvaluated, uint32_t upperEvaluated, uint32_t lowerResult, uint32_t upperResult, uint32_t result);
extern "C" __declspec(noreturn) void __cdecl guideXosNativeAotFirstRootMembershipResultBoundary(uint32_t result);
'@
            if ($isFirstRootHeapResolutionOrCondemned) {
                $heapResolutionCompletionAttribute = if ($isFirstRootCondemnedGenerationDecisionOrPreMark) { "" } else { "__declspec(noreturn) " }
                $membershipDeclaration += [Environment]::NewLine + @'
extern "C" void __cdecl guideXosNativeAotFirstRootHeapResolutionRequested(uintptr_t object);
extern "C" void __cdecl guideXosNativeAotFirstRootHeapResolutionEntered(uintptr_t object, uintptr_t threadHeap, uint32_t threadNumber);
extern "C" HEAP_RESOLUTION_COMPLETION_ATTRIBUTEvoid __cdecl guideXosNativeAotFirstRootHeapResolutionCompleted(uintptr_t object, uintptr_t threadHeap, uintptr_t heap, uint32_t heapNumber, uint32_t totalHeapCount);
'@.TrimEnd()
                $membershipDeclaration = $membershipDeclaration.Replace('HEAP_RESOLUTION_COMPLETION_ATTRIBUTE', $heapResolutionCompletionAttribute)
                if ($isFirstRootCondemnedGenerationDecisionOrPreMark) {
                    $condemnedCompletionAttribute = if ($isFirstRootPreMarkBoundary) { "" } else { "__declspec(noreturn) " }
                    $membershipDeclaration += [Environment]::NewLine + @'
extern "C" void __cdecl guideXosNativeAotFirstRootCondemnedGenerationDecisionRequested(uintptr_t object);
extern "C" void __cdecl guideXosNativeAotFirstRootCondemnedGenerationDecisionEntered(uintptr_t object, uintptr_t lowerBound, uintptr_t upperBound, int condemnedGeneration, int maximumGeneration, uintptr_t generationTable, uintptr_t generationTableIndex, uint32_t minimumSegmentSizeShift);
extern "C" void __cdecl guideXosNativeAotFirstRootCondemnedGenerationQueryStart(uintptr_t object, uintptr_t generationTable, uintptr_t generationTableIndex);
extern "C" void __cdecl guideXosNativeAotFirstRootCondemnedGenerationQueryCompleted(uintptr_t object, uint32_t generation);
extern "C" void __cdecl guideXosNativeAotFirstRootCondemnedGenerationSegmentLookupCompleted(uintptr_t object, uintptr_t segment);
extern "C" CONDEMNED_COMPLETION_ATTRIBUTEvoid __cdecl guideXosNativeAotFirstRootCondemnedGenerationDecisionCompleted(uintptr_t object, uint32_t result);
'@.TrimEnd()
                    $membershipDeclaration = $membershipDeclaration.Replace('CONDEMNED_COMPLETION_ATTRIBUTE', $condemnedCompletionAttribute)
                }
            }
            if ($isFirstRootPreMarkBoundary) {
                $membershipDeclaration += [Environment]::NewLine + @'
extern "C" void __cdecl guideXosNativeAotFirstRootPreMarkTrueBranchEntered(uintptr_t object, uintptr_t heapSentinel, uint32_t flags);
extern "C" void __cdecl guideXosNativeAotFirstRootPreMarkRootFlagTest(uint32_t flags, uint32_t bit, uint32_t result);
extern "C" void __cdecl guideXosNativeAotFirstRootPreMarkConservativeCheck(uintptr_t object, uint32_t enabled, uint32_t isFree);
extern "C" void __cdecl guideXosNativeAotFirstRootPreMarkDebugValidationEntered(uintptr_t object);
extern "C" void __cdecl guideXosNativeAotFirstRootPreMarkDebugValidationMethodTableRead(uintptr_t object, uintptr_t methodTable);
extern "C" void __cdecl guideXosNativeAotFirstRootPreMarkDebugValidationHeapPointer(uintptr_t object, uint32_t smallOnly, uint32_t result);
extern "C" void __cdecl guideXosNativeAotFirstRootPreMarkDebugValidationCompleted(uintptr_t object, uint32_t noRangeChecks, uint32_t verifyHeapGc, uint32_t smallHeapPointer, uint32_t largeHeapPointer);
extern "C" void __cdecl guideXosNativeAotFirstRootPreMarkBoundaryReached(uintptr_t object, uintptr_t heapSentinel, uint32_t flags, uintptr_t markHelper);
'@.TrimEnd()
                if ($isFirstRootFirstMarkMutation -or $isFirstRootPostQueueMarkDecision) {
                    $membershipDeclaration += [Environment]::NewLine + @'
extern "C" void __cdecl guideXosNativeAotFirstRootMarkHelperEntered(uintptr_t po, uintptr_t object);
extern "C" void __cdecl guideXosNativeAotFirstRootMarkWorklistWriteBefore(uintptr_t object, uintptr_t target, uintptr_t oldValue, uintptr_t slotIndex, uintptr_t cursorBefore, uintptr_t queueBase, uint32_t capacity);
extern "C" __declspec(noreturn) void __cdecl guideXosNativeAotFirstRootMarkWorklistWriteCompleted(uintptr_t object, uintptr_t target, uintptr_t oldValue, uintptr_t newValue, uintptr_t slotIndex, uintptr_t cursorAfter, uintptr_t queueBase, uint32_t capacity);
'@.TrimEnd()
                    if ($isFirstRootPostQueueMarkDecision) {
                        $membershipDeclaration += [Environment]::NewLine + @'
extern "C" void __cdecl guideXosNativeAotFirstRootPostQueueWorklistWriteCompleted(uintptr_t object, uintptr_t target, uintptr_t oldValue, uintptr_t newValue, uintptr_t slotIndex, uintptr_t cursorAfter, uintptr_t queueBase, uint32_t capacity);
extern "C" void __cdecl guideXosNativeAotFirstRootPostQueueDecisionRequested(uintptr_t object, uintptr_t target, uintptr_t oldValue, uintptr_t newValue, uintptr_t slotIndex, uintptr_t cursorBefore, uintptr_t cursorAfter, uintptr_t queueBase, uint32_t capacity);
extern "C" void __cdecl guideXosNativeAotFirstRootPostQueueDecisionEntered(uintptr_t oldObject);
extern "C" __declspec(noreturn) void __cdecl guideXosNativeAotFirstRootPostQueueNullDecisionCompleted(uintptr_t oldObject);
extern "C" void __cdecl guideXosNativeAotFirstRootPostQueueNonNullDecisionStarted(uintptr_t oldObject);
extern "C" void __cdecl guideXosNativeAotFirstRootPostQueueMarkedRequested(uintptr_t oldObject);
extern "C" void __cdecl guideXosNativeAotFirstRootPostQueueMarkedCompleted(uintptr_t oldObject, uint32_t result);
extern "C" __declspec(noreturn) void __cdecl guideXosNativeAotFirstRootPostQueueMarkedTrueBoundary(uintptr_t oldObject);
extern "C" void __cdecl guideXosNativeAotFirstRootPostQueueMarkedFalseBoundary(uintptr_t oldObject);
'@.TrimEnd()
                    }
                }
            }
            if ($isC14QueueInstrumentation) {
                $membershipDeclaration += [Environment]::NewLine + @'
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOMarkHelperEntered(uintptr_t po, uintptr_t object);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOCallbackEntered(uintptr_t rawArg1, uintptr_t rawArg2, uintptr_t rawArg3, uintptr_t callbackAddress, uintptr_t returnAddress, uintptr_t stackPointer);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOCandidateLoaded(uintptr_t rawValue);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOWorklistWriteBefore(uintptr_t object, uintptr_t target, uintptr_t oldValue, uintptr_t slotIndex, uintptr_t cursorBefore, uintptr_t queueBase, uint32_t capacity);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOWorklistWriteCompleted(uintptr_t object, uintptr_t target, uintptr_t oldValue, uintptr_t newValue, uintptr_t slotIndex, uintptr_t cursorAfter, uintptr_t queueBase, uint32_t capacity);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldODecisionRequested(uintptr_t object, uintptr_t target, uintptr_t oldValue, uintptr_t newValue, uintptr_t slotIndex, uintptr_t cursorBefore, uintptr_t cursorAfter, uintptr_t queueBase, uint32_t capacity);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldODecisionEntered(uintptr_t oldObject);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldONullDecisionCompleted(uintptr_t oldObject);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldONonNullDecisionStarted(uintptr_t oldObject);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOMarkedRequested(uintptr_t oldObject);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOMarkedCompleted(uintptr_t oldObject, uint32_t result);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldORawMarkWordRead(uintptr_t object, uintptr_t headerAddress, uintptr_t rawHeader, uintptr_t markMask);
extern "C" __declspec(noreturn) void __cdecl guideXosNativeAotFirstRootNonNullOldOMarkedTrueBoundary(uintptr_t oldObject);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOMarkedFalseBoundary(uintptr_t oldObject);
'@.TrimEnd()
            }
            if ($isC14QueueInstrumentation) {
                $gcWksText = $gcWksText.Replace(
                    'namespace WKS {',
                    $membershipDeclaration.TrimEnd() + [Environment]::NewLine + 'namespace WKS {')
            } else {
                $gcWksText = '#include <intrin.h>' + [Environment]::NewLine + $membershipDeclaration.TrimEnd() + [Environment]::NewLine + $gcWksText
            }
        }
        if ($isC14QueueInstrumentation) {
            $c14Declarations = @'
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOMarkHelperEntered(uintptr_t po, uintptr_t object);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOCallbackEntered(uintptr_t rawArg1, uintptr_t rawArg2, uintptr_t rawArg3, uintptr_t callbackAddress, uintptr_t returnAddress, uintptr_t stackPointer);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOCandidateLoaded(uintptr_t rawValue);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOWorklistWriteBefore(uintptr_t object, uintptr_t target, uintptr_t oldValue, uintptr_t slotIndex, uintptr_t cursorBefore, uintptr_t queueBase, uint32_t capacity);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOWorklistWriteCompleted(uintptr_t object, uintptr_t target, uintptr_t oldValue, uintptr_t newValue, uintptr_t slotIndex, uintptr_t cursorAfter, uintptr_t queueBase, uint32_t capacity);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldODecisionRequested(uintptr_t object, uintptr_t target, uintptr_t oldValue, uintptr_t newValue, uintptr_t slotIndex, uintptr_t cursorBefore, uintptr_t cursorAfter, uintptr_t queueBase, uint32_t capacity);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldODecisionEntered(uintptr_t oldObject);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldONullDecisionCompleted(uintptr_t oldObject);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldONonNullDecisionStarted(uintptr_t oldObject);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOMarkedRequested(uintptr_t oldObject);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOMarkedCompleted(uintptr_t oldObject, uint32_t result);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldORawMarkWordRead(uintptr_t object, uintptr_t headerAddress, uintptr_t rawHeader, uintptr_t markMask);
extern "C" __declspec(noreturn) void __cdecl guideXosNativeAotFirstRootNonNullOldOMarkedTrueBoundary(uintptr_t oldObject);
extern "C" __declspec(noreturn) void __cdecl guideXosNativeAotFirstRootNonNullOldOMarkedFalseBoundary(uintptr_t oldObject);
'@
            if ($isNextGenuineRootProvider) {
                $c14Declarations += [Environment]::NewLine + @'
extern "C" void __cdecl guideXosNativeAotC011EC15QueueMarkReturned(uintptr_t object, uintptr_t slot, uintptr_t oldValue, uintptr_t newValue, uintptr_t slotIndex, uintptr_t cursorBefore, uintptr_t cursorAfter, uintptr_t queueBase);
extern "C" void __cdecl guideXosNativeAotC011EC15MarkHelperReturned(uintptr_t object);
extern "C" void __cdecl guideXosNativeAotC011EC15PromoteReturned(uintptr_t object);
extern "C" void __cdecl guideXosNativeAotC011EC15PromoteEntered(uintptr_t rawArg1, uintptr_t rawArg2, uintptr_t rawArg3, uintptr_t callbackAddress, uintptr_t returnAddress, uintptr_t stackPointer);
extern "C" void __cdecl guideXosNativeAotC011EC15PromoteCandidateLoaded(uintptr_t object);
'@.TrimEnd()
                if ($isC011EC27) {
                    $c14Declarations += [Environment]::NewLine + @'
extern "C" void __cdecl guideXosNativeAotC011EC27QueueItemConsumed(uintptr_t queueOwner, uintptr_t queueBase, uintptr_t slotAddress, uintptr_t slotIndex, uintptr_t cursorBefore, uintptr_t object, uintptr_t slotValueAfter, uintptr_t cursorAfterConsumption);
extern "C" void __cdecl guideXosNativeAotC011EC27MarkStateRead(uintptr_t object, uintptr_t headerAddress, uintptr_t rawHeader, uintptr_t markMask, uintptr_t result);
extern "C" void __cdecl guideXosNativeAotC011EC27MarkWriteAttempted(uintptr_t object, uintptr_t headerAddress, uintptr_t rawHeaderBefore, uintptr_t markMask);
extern "C" void __cdecl guideXosNativeAotC011EC27MarkWriteCompleted(uintptr_t object, uintptr_t headerAddress, uintptr_t rawHeaderAfter, uintptr_t markMask, uintptr_t cursorAfter);
extern "C" void __cdecl guideXosNativeAotC011EC27ChildScanAttempted(uintptr_t parent, uintptr_t methodTable, uintptr_t objectSize);
extern "C" void __cdecl guideXosNativeAotC011EC27ChildReferenceRead(uintptr_t parent, uintptr_t slot, uintptr_t child, uintptr_t classification);
extern "C" void __cdecl guideXosNativeAotC011EC27ChildPromoteAttempted(uintptr_t parent, uintptr_t slot, uintptr_t child);
'@.TrimEnd()
                }
                if ($isC011EC27) {
                    $c14Declarations += [Environment]::NewLine + @'
extern "C" void __cdecl guideXosNativeAotC011EC28QueueEnqueue(uintptr_t owner, uintptr_t base, uintptr_t slotAddress, uintptr_t object, uintptr_t oldObject, uintptr_t slotIndex, uintptr_t cursorBefore, uintptr_t cursorAfter, uintptr_t capacity);
extern "C" void __cdecl guideXosNativeAotC011EC28QueueSlotVisited(uintptr_t owner, uintptr_t base, uintptr_t slotAddress, uintptr_t slotIndex, uintptr_t cursorBefore, uintptr_t object, uintptr_t slotValueAfter, uintptr_t cursorAfter);
extern "C" void __cdecl guideXosNativeAotC011EC28QueueMarkDecision(uintptr_t object, uintptr_t oldObject, uintptr_t alreadyMarked, uintptr_t source);
extern "C" void __cdecl guideXosNativeAotC011EC28QueueObjectReturned(uintptr_t object, uintptr_t markState, uintptr_t newlyMarked);
extern "C" void __cdecl guideXosNativeAotC011EC28QueueEmptyTest(uintptr_t result, uintptr_t cursor, uintptr_t occupancy);
extern "C" void __cdecl guideXosNativeAotC011EC28DrainEntered();
extern "C" void __cdecl guideXosNativeAotC011EC28DrainReturned();
extern "C" void __cdecl guideXosNativeAotC011EC28ChildQueueMarkEntered();
extern "C" void __cdecl guideXosNativeAotC011EC28ChildQueueMarkReturned(uintptr_t returnedObject);
extern "C" __declspec(noreturn) void __cdecl guideXosNativeAotC011EC28DrainBoundaryEntered();
'@.TrimEnd()
                    if ($isC011EC29) {
                        $c14Declarations += [Environment]::NewLine + @'
extern "C" void __cdecl guideXosNativeAotC011EC29NextPhaseEntered(int condemned, int maxGeneration, uintptr_t scanContext, uint32_t collectionReason, uint32_t compacting, uint32_t promotion, uintptr_t heap, uint32_t generationCount, uint32_t heapNumber, uint32_t fullCollection);
'@.TrimEnd()
                        if ($isC011EC33) {
                            $c14Declarations += [Environment]::NewLine + 'extern "C" void __cdecl guideXosNativeAotC011EC33PostWeakPhase(uint32_t phase);'
                            if ($isC011EC34) {
                                $c14Declarations += [Environment]::NewLine + 'extern "C" void __cdecl guideXosNativeAotC011EC34RelocationPhaseEntered(uint32_t condemnedGeneration, uint32_t compacting);'
                                $c14Declarations += [Environment]::NewLine + 'extern "C" __declspec(noreturn) void __cdecl guideXosNativeAotC011EC34RelocationRootScanReturned();'
                            }
                        }
                    }
                }
            }
            $gcWksText = $gcWksText.Replace(
                'namespace WKS {',
                $c14Declarations.TrimEnd() + [Environment]::NewLine + 'namespace WKS {')
        }
        $candidateLoadedDeclaration = if ($isNextGenuineRootProvider) {
            'extern "C" void __cdecl guideXosNativeAotC011EC15PromoteCandidateLoaded(uintptr_t object);'
        } elseif ($isC14QueueInstrumentation) {
            'extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOCandidateLoaded(uintptr_t rawValue);'
        } elseif ($isFirstRootMembershipClassification -or $isFirstRootHeapResolutionOrCondemned) {
            'extern "C" void __cdecl guideXosNativeAotFirstRootMembershipCandidateLoaded(uintptr_t rawValue);'
        } else {
            'extern "C" __declspec(noreturn) void __cdecl guideXosNativeAotFirstRootCallbackCandidateLoaded(uintptr_t rawValue);'
        }
        if ($isNextGenuineRootProvider) {
            $callbackDeclaration = @'
extern "C" void __cdecl guideXosNativeAotC011EC15PromoteEntered(
    uintptr_t rawArg1, uintptr_t rawArg2, uintptr_t rawArg3,
    uintptr_t callbackAddress, uintptr_t returnAddress,
    uintptr_t stackPointer);
'@
        } elseif ($isC14QueueInstrumentation) {
            $callbackDeclaration = @'
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOCallbackEntered(
    uintptr_t rawArg1, uintptr_t rawArg2, uintptr_t rawArg3,
    uintptr_t callbackAddress, uintptr_t returnAddress,
    uintptr_t stackPointer);
'@
        } else {
            $callbackDeclaration = @'
extern "C" void __cdecl guideXosNativeAotFirstRootCallbackEntered(
    uintptr_t rawArg1, uintptr_t rawArg2, uintptr_t rawArg3,
    uintptr_t callbackAddress, uintptr_t returnAddress,
    uintptr_t stackPointer);
'@
        }
        $callbackDeclaration = $callbackDeclaration.TrimEnd() + [Environment]::NewLine + $candidateLoadedDeclaration
        $callbackPattern = '(?m)^void GCHeap::Promote\(Object\*\* ppObject, ScanContext\* sc, uint32_t flags\)\r?\n\{'
        $callbackFunction = if ($isNextGenuineRootProvider) { 'guideXosNativeAotC011EC15PromoteEntered' } elseif ($isC14QueueInstrumentation) { 'guideXosNativeAotFirstRootNonNullOldOCallbackEntered' } else { 'guideXosNativeAotFirstRootCallbackEntered' }
        $callbackReplacement = @"
$callbackDeclaration
void GCHeap::Promote(Object** ppObject, ScanContext* sc, uint32_t flags)
{
    $callbackFunction(
        reinterpret_cast<uintptr_t>(ppObject),
        reinterpret_cast<uintptr_t>(sc),
        static_cast<uintptr_t>(flags),
        reinterpret_cast<uintptr_t>(&GCHeap::Promote),
        *reinterpret_cast<uintptr_t*>(_AddressOfReturnAddress()),
        reinterpret_cast<uintptr_t>(_AddressOfReturnAddress()));
"@
        $gcWksInjected = [regex]::Replace($gcCppText, $callbackPattern, $callbackReplacement.TrimEnd(), 1)
        $promoteSignature = 'void GCHeap::Promote(Object** ppObject, ScanContext* sc, uint32_t flags)'
        $promoteOffset = $gcWksInjected.IndexOf($promoteSignature, [System.StringComparison]::Ordinal)
        if ($promoteOffset -lt 0) { throw "Generated Workstation GC source did not contain the injected GCHeap::Promote body." }
        $promotePrefix = $gcWksInjected.Substring(0, $promoteOffset)
        $promoteBody = $gcWksInjected.Substring($promoteOffset)
        $candidateLoadedFunction = if ($isNextGenuineRootProvider) {
            'guideXosNativeAotC011EC15PromoteCandidateLoaded'
        } elseif ($isC14QueueInstrumentation) {
            'guideXosNativeAotFirstRootNonNullOldOCandidateLoaded'
        } elseif ($isFirstRootMembershipClassification -or $isFirstRootHeapResolutionOrCondemned) {
            'guideXosNativeAotFirstRootMembershipCandidateLoaded'
        } else {
            'guideXosNativeAotFirstRootCallbackCandidateLoaded'
        }
        $promoteBody = $promoteBody.Replace(
            '    uint8_t* o = (uint8_t*)*ppObject;',
            '    uint8_t* o = (uint8_t*)*ppObject;' + [Environment]::NewLine + '    ' + $candidateLoadedFunction + '(reinterpret_cast<uintptr_t>(o));')
        if ($isNextGenuineRootProvider) {
            $promoteBody = $promoteBody.Replace(
                '    hpt->mark_object_simple (&o THREAD_NUMBER_ARG);',
                '    hpt->mark_object_simple (&o THREAD_NUMBER_ARG);' + [Environment]::NewLine + '    guideXosNativeAotC011EC15MarkHelperReturned(reinterpret_cast<uintptr_t>(o));')
            $promoteBody = $promoteBody.Replace(
                '    STRESS_LOG_ROOT_PROMOTE(ppObject, o, o ? header(o)->GetMethodTable() : NULL);',
                '    STRESS_LOG_ROOT_PROMOTE(ppObject, o, o ? header(o)->GetMethodTable() : NULL);' + [Environment]::NewLine + '    guideXosNativeAotC011EC15PromoteReturned(reinterpret_cast<uintptr_t>(o));')
            if ($promoteBody -notmatch 'guideXosNativeAotC011EC15MarkHelperReturned' -or
                $promoteBody -notmatch 'guideXosNativeAotC011EC15PromoteReturned') {
                throw "C011EC15 Promote return instrumentation did not match mark_object_simple/STRESS_LOG_ROOT_PROMOTE."
            }
        }
        if ($isFirstRootMembershipClassification -or $isFirstRootHeapResolutionOrCondemned) {
            $membershipBoundarySource = if ($isFirstRootHeapResolutionOrCondemned) { @'
    if (!gc_heap::is_in_find_object_range (o))
    {
        guideXosNativeAotFirstRootMembershipResultBoundary(0u);
        return;
    }
    guideXosNativeAotFirstRootHeapResolutionRequested(reinterpret_cast<uintptr_t>(o));
'@ } else { @'
    if (!gc_heap::is_in_find_object_range (o))
    {
        guideXosNativeAotFirstRootMembershipResultBoundary(0u);
        return;
    }
    guideXosNativeAotFirstRootMembershipResultBoundary(1u);
'@ }
            $promoteBody = $promoteBody.Replace(
                @'
    if (!gc_heap::is_in_find_object_range (o))
    {
        return;
    }
'@,
                $membershipBoundarySource)
        }
        if ($isFirstRootHeapResolutionOrCondemned) {
            $heapResolutionSource = @'
    HEAP_FROM_THREAD;
    guideXosNativeAotFirstRootHeapResolutionEntered(
        reinterpret_cast<uintptr_t>(o), reinterpret_cast<uintptr_t>(hpt),
        static_cast<uint32_t>(thread));

    gc_heap* hp = gc_heap::heap_of (o);
    guideXosNativeAotFirstRootHeapResolutionCompleted(
        reinterpret_cast<uintptr_t>(o), reinterpret_cast<uintptr_t>(hpt),
        reinterpret_cast<uintptr_t>(hp), static_cast<uint32_t>(heap_number),
        static_cast<uint32_t>(n_heaps));
'@
            $promoteBody = $promoteBody.Replace(
                @'
    HEAP_FROM_THREAD;

    gc_heap* hp = gc_heap::heap_of (o);
'@,
                $heapResolutionSource)
        }
        $gcWksInjected = $promotePrefix + $promoteBody
        if ($isFirstRootMembershipClassification -or $isFirstRootHeapResolutionOrCondemned) {
            $helperOffset = $gcWksInjected.IndexOf('bool gc_heap::is_in_find_object_range (uint8_t* o)', [System.StringComparison]::Ordinal)
            if ($helperOffset -lt 0) { throw "Locked gc.cpp membership helper definition was not found." }
            $helperEnd = $gcWksInjected.IndexOf("`n}", $helperOffset, [System.StringComparison]::Ordinal)
            if ($helperEnd -lt 0) { throw "Locked gc.cpp membership helper closing brace was not found." }
            $helperEnd += 2
            $helperText = $gcWksInjected.Substring($helperOffset, $helperEnd - $helperOffset)
            $helperText = $helperText.Replace(
                @'
    if (o == nullptr)
    {
        return false;
    }
'@,
                @'
    if (o == nullptr)
    {
        return false;
    }
    guideXosNativeAotFirstRootMembershipCheckRequested(reinterpret_cast<uintptr_t>(o));
    guideXosNativeAotFirstRootMembershipCheckEntered(reinterpret_cast<uintptr_t>(o));
'@)
            $helperText = $helperText.Replace(
                '    return ((o >= g_gc_lowest_address) && (o < bookkeeping_covered_committed));',
                @'
    uint8_t* guideXosMembershipLowerBound = g_gc_lowest_address;
    const uint32_t guideXosMembershipLowerResult =
        o >= guideXosMembershipLowerBound ? 1u : 0u;
    uint8_t* guideXosMembershipUpperBound = nullptr;
    uint32_t guideXosMembershipUpperEvaluated = 0u;
    uint32_t guideXosMembershipUpperResult = 0u;
    if (guideXosMembershipLowerResult != 0u)
    {
        guideXosMembershipUpperBound = bookkeeping_covered_committed;
        guideXosMembershipUpperEvaluated = 1u;
        guideXosMembershipUpperResult =
            o < guideXosMembershipUpperBound ? 1u : 0u;
    }
    const uint32_t guideXosMembershipResult =
        guideXosMembershipLowerResult != 0u &&
        guideXosMembershipUpperResult != 0u ? 1u : 0u;
    guideXosNativeAotFirstRootMembershipCheckCompleted(
        reinterpret_cast<uintptr_t>(o),
        reinterpret_cast<uintptr_t>(guideXosMembershipLowerBound),
        reinterpret_cast<uintptr_t>(guideXosMembershipUpperBound),
        1u, guideXosMembershipUpperEvaluated,
        guideXosMembershipLowerResult, guideXosMembershipUpperResult,
        guideXosMembershipResult);
    return guideXosMembershipResult != 0u;
'@)
            if ($helperText -notmatch 'guideXosNativeAotFirstRootMembershipCheckRequested' -or
                $helperText -notmatch 'guideXosNativeAotFirstRootMembershipCheckCompleted' -or
                $helperText -notmatch 'guideXosMembershipLowerBound' -or
                $helperText -notmatch 'bookkeeping_covered_committed') {
                throw "Locked gc.cpp membership helper injection did not preserve the active USE_REGIONS/FEATURE_CONSERVATIVE_GC path."
            }
            $gcWksInjected = $gcWksInjected.Substring(0, $helperOffset) + $helperText + $gcWksInjected.Substring($helperEnd)
        }
        if ($isFirstRootCondemnedGenerationDecisionOrPreMark) {
            $condemnedHelperOffset = $gcWksInjected.IndexOf('bool gc_heap::is_in_condemned_gc (uint8_t* o)', [System.StringComparison]::Ordinal)
            if ($condemnedHelperOffset -lt 0) { throw "Locked gc.cpp condemned-generation helper definition was not found." }
            $condemnedHelperEnd = $gcWksInjected.IndexOf("`n}", $condemnedHelperOffset, [System.StringComparison]::Ordinal)
            if ($condemnedHelperEnd -lt 0) { throw "Locked gc.cpp condemned-generation helper closing brace was not found." }
            $condemnedHelperEnd += 2
            $condemnedHelperText = $gcWksInjected.Substring($condemnedHelperOffset, $condemnedHelperEnd - $condemnedHelperOffset)
            $condemnedHelperText = $condemnedHelperText.Replace(
                "{`n    assert ((o >= g_gc_lowest_address) && (o < g_gc_highest_address));",
                "{`n    guideXosNativeAotFirstRootCondemnedGenerationDecisionRequested(reinterpret_cast<uintptr_t>(o));`n    assert ((o >= g_gc_lowest_address) && (o < g_gc_highest_address));")
            $condemnedHelperText = $condemnedHelperText.Replace(
                "    int condemned_gen = settings.condemned_generation;",
                "    int condemned_gen = settings.condemned_generation;`n    const size_t guideXosCondemnedRegionIndex = (size_t)o >> gc_heap::min_segment_size_shr;`n    guideXosNativeAotFirstRootCondemnedGenerationDecisionEntered(`n        reinterpret_cast<uintptr_t>(o), reinterpret_cast<uintptr_t>(g_gc_lowest_address),`n        reinterpret_cast<uintptr_t>(g_gc_highest_address), condemned_gen, max_generation,`n        reinterpret_cast<uintptr_t>(gc_heap::map_region_to_generation_skewed),`n        static_cast<uintptr_t>(guideXosCondemnedRegionIndex),`n        static_cast<uint32_t>(gc_heap::min_segment_size_shr));")
            $condemnedHelperText = $condemnedHelperText.Replace(
                "        int gen = get_region_gen_num (o);",
                "        guideXosNativeAotFirstRootCondemnedGenerationQueryStart(`n            reinterpret_cast<uintptr_t>(o), reinterpret_cast<uintptr_t>(gc_heap::map_region_to_generation_skewed),`n            static_cast<uintptr_t>(guideXosCondemnedRegionIndex));`n        int gen = get_region_gen_num (o);`n        guideXosNativeAotFirstRootCondemnedGenerationQueryCompleted(`n            reinterpret_cast<uintptr_t>(o), static_cast<uint32_t>(gen));")
            $condemnedHelperText = $condemnedHelperText.Replace(
                "            return false;",
                "            guideXosNativeAotFirstRootCondemnedGenerationDecisionCompleted(reinterpret_cast<uintptr_t>(o), 0u);`n            return false;")
            $condemnedHelperText = $condemnedHelperText.Replace(
                "    return true;`n}",
                "    guideXosNativeAotFirstRootCondemnedGenerationDecisionCompleted(reinterpret_cast<uintptr_t>(o), 1u);`n    return true;`n}")
            $condemnedGenerationFunctionOffset = $gcWksInjected.IndexOf('int gc_heap::get_region_gen_num (uint8_t* obj)', [System.StringComparison]::Ordinal)
            if ($condemnedGenerationFunctionOffset -lt 0) { throw "Locked gc.cpp generation helper definition was not found." }
            $condemnedGenerationFunctionEnd = $gcWksInjected.IndexOf("`n}", $condemnedGenerationFunctionOffset, [System.StringComparison]::Ordinal)
            if ($condemnedGenerationFunctionEnd -lt 0) { throw "Locked gc.cpp generation helper closing brace was not found." }
            $condemnedGenerationFunctionEnd += 2
            $condemnedGenerationFunctionText = $gcWksInjected.Substring($condemnedGenerationFunctionOffset, $condemnedGenerationFunctionEnd - $condemnedGenerationFunctionOffset)
            $condemnedGenerationFunctionText = $condemnedGenerationFunctionText.Replace(
                "    assert (gen_num == heap_segment_gen_num (region_of (obj)));",
                "    heap_segment* guideXosCondemnedRegion = region_of (obj);`n    guideXosNativeAotFirstRootCondemnedGenerationSegmentLookupCompleted(`n        reinterpret_cast<uintptr_t>(obj), reinterpret_cast<uintptr_t>(guideXosCondemnedRegion));`n    assert (gen_num == heap_segment_gen_num (guideXosCondemnedRegion));")
            if ($condemnedGenerationFunctionText -notmatch 'guideXosNativeAotFirstRootCondemnedGenerationSegmentLookupCompleted') {
                throw "Locked gc.cpp generation helper segment assertion injection did not match."
            }
            $gcWksInjected = $gcWksInjected.Substring(0, $condemnedGenerationFunctionOffset) + $condemnedGenerationFunctionText + $gcWksInjected.Substring($condemnedGenerationFunctionEnd)
            if ($condemnedHelperText -notmatch 'guideXosNativeAotFirstRootCondemnedGenerationDecisionRequested' -or
                $condemnedHelperText -notmatch 'guideXosNativeAotFirstRootCondemnedGenerationDecisionEntered' -or
                $condemnedHelperText -notmatch 'guideXosNativeAotFirstRootCondemnedGenerationQueryStart' -or
                $condemnedHelperText -notmatch 'guideXosNativeAotFirstRootCondemnedGenerationDecisionCompleted') {
                throw "Locked gc.cpp condemned-generation helper injection did not match the complete USE_REGIONS decision path."
            }
            $gcWksInjected = $gcWksInjected.Substring(0, $condemnedHelperOffset) + $condemnedHelperText + $gcWksInjected.Substring($condemnedHelperEnd)
        }
        if ($isFirstRootPreMarkBoundary) {
            $promoteOffset = $gcWksInjected.IndexOf($promoteSignature, [System.StringComparison]::Ordinal)
            if ($promoteOffset -lt 0) { throw "Generated Workstation GC source lost GCHeap::Promote before pre-mark instrumentation." }
            $promotePrefix = $gcWksInjected.Substring(0, $promoteOffset)
            $promoteBody = $gcWksInjected.Substring($promoteOffset)
            $promoteBody = $promoteBody.Replace(
                '    dprintf (3, ("Promote %zx", (size_t)o));',
                @'
    guideXosNativeAotFirstRootPreMarkTrueBranchEntered(
        reinterpret_cast<uintptr_t>(o), reinterpret_cast<uintptr_t>(hpt),
        static_cast<uint32_t>(flags));
    dprintf (3, ("Promote %zx", (size_t)o));
'@.TrimEnd())
            $promoteBody = $promoteBody.Replace(
                '    if (flags & GC_CALL_INTERIOR)',
                @'
    guideXosNativeAotFirstRootPreMarkRootFlagTest(
        static_cast<uint32_t>(flags), 0x1u,
        (flags & GC_CALL_INTERIOR) != 0u ? 1u : 0u);
    if (flags & GC_CALL_INTERIOR)
'@.TrimEnd())
            $promoteBody = $promoteBody.Replace(
                '    if (flags & GC_CALL_PINNED)',
                @'
    guideXosNativeAotFirstRootPreMarkRootFlagTest(
        static_cast<uint32_t>(flags), 0x2u,
        (flags & GC_CALL_PINNED) != 0u ? 1u : 0u);
    if (flags & GC_CALL_PINNED)
'@.TrimEnd())
            $promoteBody = $promoteBody.Replace(
                @'
    if (GCConfig::GetConservativeGC()
        && ((CObjectHeader*)o)->IsFree())
'@,
                @'
    const uint32_t guideXosConservativeGcEnabled =
        GCConfig::GetConservativeGC() ? 1u : 0u;
    const uint32_t guideXosObjectIsFree =
        guideXosConservativeGcEnabled != 0u &&
        ((CObjectHeader*)o)->IsFree() ? 1u : 0u;
    guideXosNativeAotFirstRootPreMarkConservativeCheck(
        reinterpret_cast<uintptr_t>(o), guideXosConservativeGcEnabled,
        guideXosObjectIsFree);
    if (guideXosConservativeGcEnabled != 0u &&
        guideXosObjectIsFree != 0u)
'@)
            $promoteBody = $promoteBody.Replace(
                '    ((CObjectHeader*)o)->Validate();',
                @'
    guideXosNativeAotFirstRootPreMarkDebugValidationEntered(
        reinterpret_cast<uintptr_t>(o));
    ((CObjectHeader*)o)->Validate();
'@.TrimEnd())
            $promoteBody = $promoteBody.Replace(
                '    hpt->mark_object_simple (&o THREAD_NUMBER_ARG);',
                @'
    guideXosNativeAotFirstRootPreMarkBoundaryReached(
        reinterpret_cast<uintptr_t>(o), reinterpret_cast<uintptr_t>(hpt),
        static_cast<uint32_t>(flags),
        reinterpret_cast<uintptr_t>(&gc_heap::mark_object_simple));
    hpt->mark_object_simple(&o THREAD_NUMBER_ARG);
'@.TrimEnd())
            if ($promoteBody -notmatch 'guideXosNativeAotFirstRootPreMarkTrueBranchEntered' -or
                $promoteBody -notmatch 'guideXosNativeAotFirstRootPreMarkRootFlagTest' -or
                $promoteBody -notmatch 'guideXosNativeAotFirstRootPreMarkConservativeCheck' -or
                $promoteBody -notmatch 'guideXosNativeAotFirstRootPreMarkDebugValidationEntered' -or
                $promoteBody -notmatch 'guideXosNativeAotFirstRootPreMarkBoundaryReached') {
                throw "Locked GCHeap::Promote pre-mark injection did not match every required pre-mark boundary."
            }
            $gcWksInjected = $promotePrefix + $promoteBody

            $gcWksInjected = $gcWksInjected.Replace(
                '        MethodTable * pMT = GetMethodTable();',
                @'
        MethodTable * pMT = GetMethodTable();
        guideXosNativeAotFirstRootPreMarkDebugValidationMethodTableRead(
            reinterpret_cast<uintptr_t>(this), reinterpret_cast<uintptr_t>(pMT));
'@.TrimEnd())
            $gcWksInjected = $gcWksInjected.Replace(
                '            (GCConfig::GetHeapVerifyLevel() & GCConfig::HEAPVERIFY_NO_RANGE_CHECKS) == GCConfig::HEAPVERIFY_NO_RANGE_CHECKS;',
                @'
            (GCConfig::GetHeapVerifyLevel() & GCConfig::HEAPVERIFY_NO_RANGE_CHECKS) == GCConfig::HEAPVERIFY_NO_RANGE_CHECKS;
'@.TrimEnd())
            $gcWksInjected = $gcWksInjected.Replace(
                '            fSmallObjectHeapPtr = g_theGCHeap->IsHeapPointer(this, TRUE);',
                @'
            fSmallObjectHeapPtr = g_theGCHeap->IsHeapPointer(this, TRUE);
            guideXosNativeAotFirstRootPreMarkDebugValidationHeapPointer(
                reinterpret_cast<uintptr_t>(this), 1u,
                fSmallObjectHeapPtr ? 1u : 0u);
'@.TrimEnd())
            $gcWksInjected = $gcWksInjected.Replace(
                '                fLargeObjectHeapPtr = g_theGCHeap->IsHeapPointer(this);',
                @'
                fLargeObjectHeapPtr = g_theGCHeap->IsHeapPointer(this);
                guideXosNativeAotFirstRootPreMarkDebugValidationHeapPointer(
                    reinterpret_cast<uintptr_t>(this), 0u,
                    fLargeObjectHeapPtr ? 1u : 0u);
'@.TrimEnd())
            $gcWksInjected = $gcWksInjected.Replace(
                '        if (bDeep && (GCConfig::GetHeapVerifyLevel() & GCConfig::HEAPVERIFY_GC))',
                @'
        const uint32_t guideXosVerifyHeapGc =
            (GCConfig::GetHeapVerifyLevel() & GCConfig::HEAPVERIFY_GC) != 0u ? 1u : 0u;
        if (bDeep && guideXosVerifyHeapGc != 0u)
'@.TrimEnd())
            $gcWksInjected = $gcWksInjected.Replace(
                '        if (fSmallObjectHeapPtr)',
                @'
        guideXosNativeAotFirstRootPreMarkDebugValidationCompleted(
            reinterpret_cast<uintptr_t>(this), noRangeChecks ? 1u : 0u,
            guideXosVerifyHeapGc, fSmallObjectHeapPtr ? 1u : 0u,
            fLargeObjectHeapPtr ? 1u : 0u);
        if (fSmallObjectHeapPtr)
'@.TrimEnd())
            if ($gcWksInjected -notmatch 'guideXosNativeAotFirstRootPreMarkDebugValidationMethodTableRead' -or
                $gcWksInjected -notmatch 'guideXosNativeAotFirstRootPreMarkDebugValidationHeapPointer' -or
                $gcWksInjected -notmatch 'guideXosNativeAotFirstRootPreMarkDebugValidationCompleted') {
                throw "Locked CObjectHeader::Validate pre-mark instrumentation did not match all required source reads."
            }
            if ($isFirstRootFirstMarkMutation -or $isFirstRootPostQueueMarkDecision) {
                $markSimpleSignature = 'gc_heap::mark_object_simple (uint8_t** po THREAD_NUMBER_DCL)'
                $markSimpleOffset = $gcWksInjected.IndexOf($markSimpleSignature, [System.StringComparison]::Ordinal)
                if ($markSimpleOffset -lt 0) { throw "Locked mark_object_simple definition was not found for C011EC12 instrumentation." }
                $markSimpleBodyEnd = $gcWksInjected.IndexOf('inline', $markSimpleOffset, [System.StringComparison]::Ordinal)
                if ($markSimpleBodyEnd -lt 0) { throw "Locked mark_object_simple body end was not found for C011EC12 instrumentation." }
                $markSimplePrefix = $gcWksInjected.Substring(0, $markSimpleOffset)
                $markSimpleBody = $gcWksInjected.Substring($markSimpleOffset, $markSimpleBodyEnd - $markSimpleOffset)
                $markSimpleBody = $markSimpleBody.Replace(
                    '    uint8_t* o = *po;',
                    @'
    uint8_t* o = *po;
    guideXosNativeAotFirstRootMarkHelperEntered(
        reinterpret_cast<uintptr_t>(po), reinterpret_cast<uintptr_t>(o));
'@.TrimEnd())
                if ($markSimpleBody -notmatch 'guideXosNativeAotFirstRootMarkHelperEntered') {
                    throw "Locked mark_object_simple entry instrumentation did not match its root load."
                }
                $gcWksInjected = $markSimplePrefix + $markSimpleBody + $gcWksInjected.Substring($markSimpleBodyEnd)

                $queueSignature = 'uint8_t *mark_queue_t::queue_mark(uint8_t *o)'
                $queueOffset = $gcWksInjected.IndexOf($queueSignature, [System.StringComparison]::Ordinal)
                if ($queueOffset -lt 0) { throw "Locked queue_mark implementation was not found for C011EC12 instrumentation." }
                $queueEnd = $gcWksInjected.IndexOf('uint8_t *mark_queue_t::queue_mark(uint8_t *o, int condemned_gen)', $queueOffset, [System.StringComparison]::Ordinal)
                if ($queueEnd -lt 0) { throw "Locked queue_mark overload boundary was not found for C011EC12 instrumentation." }
                $queuePrefix = $gcWksInjected.Substring(0, $queueOffset)
                $queueBody = $gcWksInjected.Substring($queueOffset, $queueEnd - $queueOffset)
                $queueSequence = @'
#ifdef MARK_PHASE_PREFETCH
    Prefetch (o);

    // while the prefetch is taking effect, park our object in the queue
    // and fetch an object that has been sitting in the queue for a while
    // and where (hopefully) the memory is already in the cache
    size_t slot_index = curr_slot_index;
    uint8_t* old_o = slot_table[slot_index];
    slot_table[slot_index] = o;

    curr_slot_index = (slot_index + 1) % slot_count;
    if (old_o == nullptr)
        return nullptr;
#else //MARK_PHASE_PREFETCH
    uint8_t* old_o = o;
#endif //MARK_PHASE_PREFETCH

    // this causes us to access the method table pointer of the old object
    BOOL already_marked = marked (old_o);
    if (already_marked)
    {
        return nullptr;
    }
    set_marked (old_o);
    return old_o;
'@
                $queueReplacement = if ($isFirstRootPostQueueMarkDecision) { @'
#ifdef MARK_PHASE_PREFETCH
    Prefetch (o);

    // Proof-only C011EC13 source injection: preserve the locked queue_mark
    // sequence and observe the post-queue old_o decision before marked().
    size_t slot_index = curr_slot_index;
    uint8_t* old_o = slot_table[slot_index];
    guideXosNativeAotFirstRootMarkWorklistWriteBefore(
        reinterpret_cast<uintptr_t>(o),
        reinterpret_cast<uintptr_t>(&slot_table[slot_index]),
        reinterpret_cast<uintptr_t>(old_o),
        static_cast<uintptr_t>(slot_index),
        static_cast<uintptr_t>(curr_slot_index),
        reinterpret_cast<uintptr_t>(slot_table),
        static_cast<uint32_t>(slot_count));
    slot_table[slot_index] = o;

    curr_slot_index = (slot_index + 1) % slot_count;
    guideXosNativeAotFirstRootPostQueueWorklistWriteCompleted(
        reinterpret_cast<uintptr_t>(o),
        reinterpret_cast<uintptr_t>(&slot_table[slot_index]),
        reinterpret_cast<uintptr_t>(old_o),
        reinterpret_cast<uintptr_t>(o),
        static_cast<uintptr_t>(slot_index),
        static_cast<uintptr_t>(curr_slot_index),
        reinterpret_cast<uintptr_t>(slot_table),
        static_cast<uint32_t>(slot_count));
    guideXosNativeAotFirstRootPostQueueDecisionRequested(
        reinterpret_cast<uintptr_t>(o),
        reinterpret_cast<uintptr_t>(&slot_table[slot_index]),
        reinterpret_cast<uintptr_t>(old_o),
        reinterpret_cast<uintptr_t>(o),
        static_cast<uintptr_t>(slot_index),
        static_cast<uintptr_t>(slot_index),
        static_cast<uintptr_t>(curr_slot_index),
        reinterpret_cast<uintptr_t>(slot_table),
        static_cast<uint32_t>(slot_count));
    guideXosNativeAotFirstRootPostQueueDecisionEntered(
        reinterpret_cast<uintptr_t>(old_o));
    if (old_o == nullptr)
    {
        guideXosNativeAotFirstRootPostQueueNullDecisionCompleted(
            reinterpret_cast<uintptr_t>(old_o));
        return nullptr;
    }
    guideXosNativeAotFirstRootPostQueueNonNullDecisionStarted(
        reinterpret_cast<uintptr_t>(old_o));
    guideXosNativeAotFirstRootPostQueueMarkedRequested(
        reinterpret_cast<uintptr_t>(old_o));
    BOOL already_marked = marked (old_o);
    guideXosNativeAotFirstRootPostQueueMarkedCompleted(
        reinterpret_cast<uintptr_t>(old_o), already_marked ? 1u : 0u);
    if (already_marked)
    {
        guideXosNativeAotFirstRootPostQueueMarkedTrueBoundary(
            reinterpret_cast<uintptr_t>(old_o));
        return nullptr;
    }
    guideXosNativeAotFirstRootPostQueueMarkedFalseBoundary(
        reinterpret_cast<uintptr_t>(old_o));
    set_marked (old_o);
    return old_o;
#else //MARK_PHASE_PREFETCH
    uint8_t* old_o = o;
    BOOL already_marked = marked (old_o);
    if (already_marked)
    {
        return nullptr;
    }
    set_marked (old_o);
    return old_o;
#endif //MARK_PHASE_PREFETCH
'@ } else { @'
#ifdef MARK_PHASE_PREFETCH
    Prefetch (o);
    size_t slot_index = curr_slot_index;
    uint8_t* old_o = slot_table[slot_index];
    guideXosNativeAotFirstRootMarkWorklistWriteBefore(
        reinterpret_cast<uintptr_t>(o),
        reinterpret_cast<uintptr_t>(&slot_table[slot_index]),
        reinterpret_cast<uintptr_t>(old_o),
        static_cast<uintptr_t>(slot_index),
        static_cast<uintptr_t>(curr_slot_index),
        reinterpret_cast<uintptr_t>(slot_table),
        static_cast<uint32_t>(slot_count));
    slot_table[slot_index] = o;

    curr_slot_index = (slot_index + 1) % slot_count;
    guideXosNativeAotFirstRootMarkWorklistWriteCompleted(
        reinterpret_cast<uintptr_t>(o),
        reinterpret_cast<uintptr_t>(&slot_table[slot_index]),
        reinterpret_cast<uintptr_t>(old_o),
        reinterpret_cast<uintptr_t>(o),
        static_cast<uintptr_t>(slot_index),
        static_cast<uintptr_t>(curr_slot_index),
        reinterpret_cast<uintptr_t>(slot_table),
        static_cast<uint32_t>(slot_count));
#else //MARK_PHASE_PREFETCH
    uint8_t* old_o = o;
    guideXosNativeAotFirstRootMarkWorklistWriteBefore(
        reinterpret_cast<uintptr_t>(o),
        reinterpret_cast<uintptr_t>(&old_o),
        reinterpret_cast<uintptr_t>(old_o),
        0u,
        0u,
        reinterpret_cast<uintptr_t>(&old_o),
        static_cast<uint32_t>(slot_count));
    guideXosNativeAotFirstRootMarkWorklistWriteCompleted(
        reinterpret_cast<uintptr_t>(o),
        reinterpret_cast<uintptr_t>(&old_o),
        reinterpret_cast<uintptr_t>(old_o),
        reinterpret_cast<uintptr_t>(o),
        0u,
        0u,
        reinterpret_cast<uintptr_t>(&old_o),
        static_cast<uint32_t>(slot_count));
#endif //MARK_PHASE_PREFETCH
'@ }
                if (-not $queueBody.Contains($queueSequence)) { throw "Locked MARK_PHASE_PREFETCH queue_mark write sequence did not match C011EC12 instrumentation." }
                $queueBody = $queueBody.Replace($queueSequence, $queueReplacement.TrimEnd())
                if ($queueBody -notmatch 'guideXosNativeAotFirstRootMarkWorklistWriteBefore' -or
                    ($isFirstRootPostQueueMarkDecision -and $queueBody -notmatch 'guideXosNativeAotFirstRootPostQueueWorklistWriteCompleted') -or
                    ($isFirstRootFirstMarkMutation -and $queueBody -notmatch 'guideXosNativeAotFirstRootMarkWorklistWriteCompleted') -or
                    ($isFirstRootPostQueueMarkDecision -and $queueBody -notmatch 'guideXosNativeAotFirstRootPostQueueDecisionRequested')) {
                    throw "Locked queue_mark post-queue instrumentation did not match the selected proof boundary."
                }
                $gcWksInjected = $queuePrefix + $queueBody + $gcWksInjected.Substring($queueEnd)
            }
        }
        if ($isNextGenuineRootProvider) {
            $queueSignature = 'uint8_t *mark_queue_t::queue_mark(uint8_t *o)'
            $queueOffset = $gcWksInjected.IndexOf($queueSignature, [System.StringComparison]::Ordinal)
            if ($queueOffset -lt 0) { throw "Locked queue_mark implementation was not found for C011EC15 instrumentation." }
            $queueEnd = $gcWksInjected.IndexOf('uint8_t *mark_queue_t::queue_mark(uint8_t *o, int condemned_gen)', $queueOffset, [System.StringComparison]::Ordinal)
            if ($queueEnd -lt 0) { throw "Locked queue_mark overload boundary was not found for C011EC15 instrumentation." }
            $queuePrefix = $gcWksInjected.Substring(0, $queueOffset)
            $queueBody = $gcWksInjected.Substring($queueOffset, $queueEnd - $queueOffset)
            $c15QueueSequence = @'
#ifdef MARK_PHASE_PREFETCH
    Prefetch (o);

    // while the prefetch is taking effect, park our object in the queue
    // and fetch an object that has been sitting in the queue for a while
    // and where (hopefully) the memory is already in the cache
    size_t slot_index = curr_slot_index;
    uint8_t* old_o = slot_table[slot_index];
    slot_table[slot_index] = o;

    curr_slot_index = (slot_index + 1) % slot_count;
    if (old_o == nullptr)
        return nullptr;
#else //MARK_PHASE_PREFETCH
    uint8_t* old_o = o;
#endif //MARK_PHASE_PREFETCH

    // this causes us to access the method table pointer of the old object
    BOOL already_marked = marked (old_o);
    if (already_marked)
    {
        return nullptr;
    }
    set_marked (old_o);
    return old_o;
'@
            $c15QueueReplacement = @'
#ifdef MARK_PHASE_PREFETCH
    Prefetch (o);
    size_t slot_index = curr_slot_index;
    uint8_t* old_o = slot_table[slot_index];
    slot_table[slot_index] = o;
    curr_slot_index = (slot_index + 1) % slot_count;
    guideXosNativeAotC011EC15QueueMarkReturned(
        reinterpret_cast<uintptr_t>(o),
        reinterpret_cast<uintptr_t>(&slot_table[slot_index]),
        reinterpret_cast<uintptr_t>(old_o),
        reinterpret_cast<uintptr_t>(o),
        static_cast<uintptr_t>(slot_index),
        static_cast<uintptr_t>(slot_index),
        static_cast<uintptr_t>(curr_slot_index),
        reinterpret_cast<uintptr_t>(slot_table));
    if (old_o == nullptr)
        return nullptr;
#else //MARK_PHASE_PREFETCH
    uint8_t* old_o = o;
#endif //MARK_PHASE_PREFETCH

    BOOL already_marked = marked (old_o);
    if (already_marked)
    {
        return nullptr;
    }
    set_marked (old_o);
    return old_o;
'@
            if ($isC011EC28) {
                $c15QueueReplacement = $c15QueueReplacement.Replace(
                    @'
    curr_slot_index = (slot_index + 1) % slot_count;
    guideXosNativeAotC011EC15QueueMarkReturned(
'@,
                    @'
    curr_slot_index = (slot_index + 1) % slot_count;
    guideXosNativeAotC011EC28QueueEnqueue(
        reinterpret_cast<uintptr_t>(this),
        reinterpret_cast<uintptr_t>(slot_table),
        reinterpret_cast<uintptr_t>(&slot_table[slot_index]),
        reinterpret_cast<uintptr_t>(o),
        reinterpret_cast<uintptr_t>(old_o),
        static_cast<uintptr_t>(slot_index),
        static_cast<uintptr_t>(slot_index),
        static_cast<uintptr_t>(curr_slot_index),
        static_cast<uintptr_t>(slot_count));
    guideXosNativeAotC011EC15QueueMarkReturned(
'@)
                $c15QueueReplacement = $c15QueueReplacement.Replace(
                    @'
    BOOL already_marked = marked (old_o);
'@,
                    @'
    BOOL already_marked = marked (old_o);
    guideXosNativeAotC011EC28QueueMarkDecision(
        reinterpret_cast<uintptr_t>(old_o), reinterpret_cast<uintptr_t>(old_o),
        already_marked ? 1u : 0u, 1u);
'@)
                $c15QueueReplacement = $c15QueueReplacement.Replace(
                    @'
    set_marked (old_o);
'@,
                    @'
    guideXosNativeAotC011EC27MarkWriteAttempted(
        reinterpret_cast<uintptr_t>(old_o), reinterpret_cast<uintptr_t>(old_o),
        reinterpret_cast<uintptr_t>(header(old_o)->RawGetMethodTable()),
        static_cast<uintptr_t>(GC_MARKED));
    set_marked (old_o);
    guideXosNativeAotC011EC27MarkWriteCompleted(
        reinterpret_cast<uintptr_t>(old_o), reinterpret_cast<uintptr_t>(old_o),
        reinterpret_cast<uintptr_t>(header(old_o)->RawGetMethodTable()),
        static_cast<uintptr_t>(GC_MARKED),
        static_cast<uintptr_t>(curr_slot_index));
'@)
            }
            if (-not $queueBody.Contains($c15QueueSequence)) { throw "Locked MARK_PHASE_PREFETCH queue_mark sequence did not match C011EC15 instrumentation." }
            $queueBody = $queueBody.Replace($c15QueueSequence, $c15QueueReplacement.TrimEnd())
            $gcWksInjected = $queuePrefix + $queueBody + $gcWksInjected.Substring($queueEnd)
        }
        if ($isC011EC27) {
            $markReadSequence = @'
    BOOL IsMarked() const
    {
        return !!(((size_t)RawGetMethodTable()) & GC_MARKED);
    }
'@
            $markReadReplacement = @'
    BOOL IsMarked() const
    {
        const uintptr_t guideXosRawHeader =
            reinterpret_cast<uintptr_t>(RawGetMethodTable());
        guideXosNativeAotC011EC27MarkStateRead(
            reinterpret_cast<uintptr_t>(this),
            reinterpret_cast<uintptr_t>(this),
            guideXosRawHeader,
            static_cast<uintptr_t>(GC_MARKED),
            (guideXosRawHeader & GC_MARKED) != 0u ? 1u : 0u);
        return !!(guideXosRawHeader & GC_MARKED);
    }
'@
            if (-not $gcWksInjected.Contains($markReadSequence)) {
                throw "Locked CObjectHeader::IsMarked source did not match C011EC27 instrumentation."
            }
            $gcWksInjected = $gcWksInjected.Replace($markReadSequence, $markReadReplacement.TrimEnd())

            $nextMarkedSignature = 'uint8_t* mark_queue_t::get_next_marked()'
            $nextMarkedOffset = $gcWksInjected.IndexOf($nextMarkedSignature, [System.StringComparison]::Ordinal)
            if ($nextMarkedOffset -lt 0) {
                throw "Locked mark_queue_t::get_next_marked implementation was not found for C011EC27 instrumentation."
            }
            $nextMarkedEnd = $gcWksInjected.IndexOf('void mark_queue_t::verify_empty()', $nextMarkedOffset, [System.StringComparison]::Ordinal)
            if ($nextMarkedEnd -lt 0) {
                throw "Locked mark_queue_t::verify_empty boundary was not found for C011EC27 instrumentation."
            }
            $nextMarkedPrefix = $gcWksInjected.Substring(0, $nextMarkedOffset)
            $nextMarkedBody = $gcWksInjected.Substring($nextMarkedOffset, $nextMarkedEnd - $nextMarkedOffset)
            $nextMarkedSequence = @'
        uint8_t* o = slot_table[slot_index];
        slot_table[slot_index] = nullptr;
        slot_index = (slot_index + 1) % slot_count;
'@
            $nextMarkedReplacement = @'
        const size_t guideXosConsumedSlotIndex = slot_index;
        const size_t guideXosQueueCursorBefore = curr_slot_index;
        uint8_t* o = slot_table[slot_index];
        slot_table[slot_index] = nullptr;
        slot_index = (slot_index + 1) % slot_count;
        guideXosNativeAotC011EC28QueueSlotVisited(
            reinterpret_cast<uintptr_t>(this),
            reinterpret_cast<uintptr_t>(slot_table),
            reinterpret_cast<uintptr_t>(&slot_table[guideXosConsumedSlotIndex]),
            static_cast<uintptr_t>(guideXosConsumedSlotIndex),
            static_cast<uintptr_t>(guideXosQueueCursorBefore),
            reinterpret_cast<uintptr_t>(o),
            reinterpret_cast<uintptr_t>(slot_table[guideXosConsumedSlotIndex]),
            static_cast<uintptr_t>(slot_index));
        if (o != nullptr)
        {
            guideXosNativeAotC011EC27QueueItemConsumed(
                reinterpret_cast<uintptr_t>(this),
                reinterpret_cast<uintptr_t>(slot_table),
                reinterpret_cast<uintptr_t>(&slot_table[guideXosConsumedSlotIndex]),
                static_cast<uintptr_t>(guideXosConsumedSlotIndex),
                static_cast<uintptr_t>(guideXosQueueCursorBefore),
                reinterpret_cast<uintptr_t>(o),
                reinterpret_cast<uintptr_t>(slot_table[guideXosConsumedSlotIndex]),
                static_cast<uintptr_t>(slot_index));
        }
'@
            if (-not $nextMarkedBody.Contains($nextMarkedSequence)) {
                throw "Locked get_next_marked dequeue sequence did not match C011EC27 instrumentation."
            }
            $nextMarkedBody = $nextMarkedBody.Replace($nextMarkedSequence, $nextMarkedReplacement.TrimEnd())
            $nextMarkedBody = $nextMarkedBody.Replace(
                @'
                set_marked (o);
                curr_slot_index = slot_index;
                return o;
'@,
                @'
                guideXosNativeAotC011EC27MarkWriteAttempted(
                    reinterpret_cast<uintptr_t>(o),
                    reinterpret_cast<uintptr_t>(o),
                    reinterpret_cast<uintptr_t>(header(o)->RawGetMethodTable()),
                    static_cast<uintptr_t>(GC_MARKED));
                set_marked (o);
                curr_slot_index = slot_index;
                guideXosNativeAotC011EC27MarkWriteCompleted(
                    reinterpret_cast<uintptr_t>(o),
                    reinterpret_cast<uintptr_t>(o),
                    reinterpret_cast<uintptr_t>(header(o)->RawGetMethodTable()),
                    static_cast<uintptr_t>(GC_MARKED),
                    static_cast<uintptr_t>(curr_slot_index));
                guideXosNativeAotC011EC28QueueObjectReturned(
                    reinterpret_cast<uintptr_t>(o), 1u, 1u);
                guideXosNativeAotC011EC28QueueEmptyTest(
                    1u, static_cast<uintptr_t>(curr_slot_index), 0u);
                return o;
'@.TrimEnd())
            if ($isC011EC28) {
                $nextMarkedBody = $nextMarkedBody.Replace(
                    @'
    return nullptr;
}
'@,
                    @'
    guideXosNativeAotC011EC28QueueEmptyTest(
        0u, static_cast<uintptr_t>(curr_slot_index), 0u);
    return nullptr;
}
'@)
                $nextMarkedBody = $nextMarkedBody.Replace(
                    @'
            BOOL already_marked = marked (o);
'@,
                    @'
            BOOL already_marked = marked (o);
            guideXosNativeAotC011EC28QueueMarkDecision(
                reinterpret_cast<uintptr_t>(o), 0u,
                already_marked ? 1u : 0u, 2u);
'@)
            }
            if ($nextMarkedBody -notmatch 'guideXosNativeAotC011EC27QueueItemConsumed' -or
                $nextMarkedBody -notmatch 'guideXosNativeAotC011EC27MarkWriteAttempted' -or
                $nextMarkedBody -notmatch 'guideXosNativeAotC011EC27MarkWriteCompleted') {
                throw "Locked get_next_marked mark path did not match C011EC27 instrumentation."
            }
            $gcWksInjected = $nextMarkedPrefix + $nextMarkedBody + $gcWksInjected.Substring($nextMarkedEnd)

            $drainSignature = 'void gc_heap::drain_mark_queue ()'
            $drainOffset = $gcWksInjected.IndexOf($drainSignature, [System.StringComparison]::Ordinal)
            if ($drainOffset -lt 0) {
                throw "Locked gc_heap::drain_mark_queue implementation was not found for C011EC27 instrumentation."
            }
            $drainEnd = $gcWksInjected.IndexOf('#ifdef BACKGROUND_GC', $drainOffset, [System.StringComparison]::Ordinal)
            if ($drainEnd -lt 0) {
                throw "Locked background-GC boundary was not found after drain_mark_queue for C011EC27 instrumentation."
            }
            $drainPrefix = $gcWksInjected.Substring(0, $drainOffset)
            $drainBody = $gcWksInjected.Substring($drainOffset, $drainEnd - $drainOffset)
            $drainBody = $drainBody.Replace(
                @'
        if (contain_pointers_or_collectible (o))
        {
            go_through_object_cl (method_table(o), o, s, poo,
'@,
                @'
        if (contain_pointers_or_collectible (o))
        {
            guideXosNativeAotC011EC27ChildScanAttempted(
                reinterpret_cast<uintptr_t>(o),
                reinterpret_cast<uintptr_t>(method_table(o)),
                static_cast<uintptr_t>(s));
            go_through_object_cl (method_table(o), o, s, poo,
'@.TrimEnd())
            $drainBody = $drainBody.Replace(
                '                                        uint8_t* oo = mark_queue.queue_mark(*poo, condemned_gen);',
                @'
                                        guideXosNativeAotC011EC27ChildReferenceRead(
                                            reinterpret_cast<uintptr_t>(o),
                                            reinterpret_cast<uintptr_t>(poo),
                                            reinterpret_cast<uintptr_t>(*poo),
                                            *poo != nullptr ? 1u : 0u);
                                         guideXosNativeAotC011EC27ChildPromoteAttempted(
                                             reinterpret_cast<uintptr_t>(o),
                                             reinterpret_cast<uintptr_t>(poo),
                                             reinterpret_cast<uintptr_t>(*poo));
                                         guideXosNativeAotC011EC28ChildQueueMarkEntered();
                                         uint8_t* oo = mark_queue.queue_mark(*poo, condemned_gen);
                                         guideXosNativeAotC011EC28ChildQueueMarkReturned(
                                             reinterpret_cast<uintptr_t>(oo));
'@.TrimEnd())
            if ($drainBody -notmatch 'guideXosNativeAotC011EC27ChildScanAttempted' -or
                $drainBody -notmatch 'guideXosNativeAotC011EC27ChildReferenceRead' -or
                $drainBody -notmatch 'guideXosNativeAotC011EC27ChildPromoteAttempted') {
                throw "Locked drain_mark_queue child path did not match C011EC27 instrumentation."
            }
            if ($isC011EC28) {
                $drainOpen = @'
void gc_heap::drain_mark_queue ()
{
'@
                $drainBody = $drainBody.Replace(
                    $drainOpen,
                    @'
void gc_heap::drain_mark_queue ()
{
    guideXosNativeAotC011EC28DrainEntered();
'@)
                $drainClose = $drainBody.LastIndexOf("`n}")
                if ($drainClose -lt 0) { throw "Locked drain_mark_queue closing boundary was not found for C011EC28 instrumentation." }
                $drainBody = $drainBody.Substring(0, $drainClose) +
                    "`n    guideXosNativeAotC011EC28DrainReturned();" +
                    $drainBody.Substring($drainClose)
            }
            $gcWksInjected = $drainPrefix + $drainBody + $gcWksInjected.Substring($drainEnd)
        }
        if ($isC14QueueInstrumentation -and -not $isNextGenuineRootProvider) {
            $markSimpleSignature = 'gc_heap::mark_object_simple (uint8_t** po THREAD_NUMBER_DCL)'
            $markSimpleOffset = $gcWksInjected.IndexOf($markSimpleSignature, [System.StringComparison]::Ordinal)
            if ($markSimpleOffset -lt 0) { throw "Locked mark_object_simple definition was not found for C011EC14 instrumentation." }
            $markSimpleBodyEnd = $gcWksInjected.IndexOf('inline', $markSimpleOffset, [System.StringComparison]::Ordinal)
            if ($markSimpleBodyEnd -lt 0) { throw "Locked mark_object_simple body end was not found for C011EC14 instrumentation." }
            $markSimplePrefix = $gcWksInjected.Substring(0, $markSimpleOffset)
            $markSimpleBody = $gcWksInjected.Substring($markSimpleOffset, $markSimpleBodyEnd - $markSimpleOffset)
            $markSimpleBody = $markSimpleBody.Replace(
                '    uint8_t* o = *po;',
                @'
    uint8_t* o = *po;
    guideXosNativeAotFirstRootNonNullOldOMarkHelperEntered(
        reinterpret_cast<uintptr_t>(po), reinterpret_cast<uintptr_t>(o));
'@.TrimEnd())
            $gcWksInjected = $markSimplePrefix + $markSimpleBody + $gcWksInjected.Substring($markSimpleBodyEnd)

            $queueSignature = 'uint8_t *mark_queue_t::queue_mark(uint8_t *o)'
            $queueOffset = $gcWksInjected.IndexOf($queueSignature, [System.StringComparison]::Ordinal)
            if ($queueOffset -lt 0) { throw "Locked queue_mark implementation was not found for C011EC14 instrumentation." }
            $queueEnd = $gcWksInjected.IndexOf('uint8_t *mark_queue_t::queue_mark(uint8_t *o, int condemned_gen)', $queueOffset, [System.StringComparison]::Ordinal)
            if ($queueEnd -lt 0) { throw "Locked queue_mark overload boundary was not found for C011EC14 instrumentation." }
            $queuePrefix = $gcWksInjected.Substring(0, $queueOffset)
            $queueBody = $gcWksInjected.Substring($queueOffset, $queueEnd - $queueOffset)
            $c14QueueSequence = @'
#ifdef MARK_PHASE_PREFETCH
    Prefetch (o);

    // while the prefetch is taking effect, park our object in the queue
    // and fetch an object that has been sitting in the queue for a while
    // and where (hopefully) the memory is already in the cache
    size_t slot_index = curr_slot_index;
    uint8_t* old_o = slot_table[slot_index];
    slot_table[slot_index] = o;

    curr_slot_index = (slot_index + 1) % slot_count;
    if (old_o == nullptr)
        return nullptr;
#else //MARK_PHASE_PREFETCH
    uint8_t* old_o = o;
#endif //MARK_PHASE_PREFETCH

    // this causes us to access the method table pointer of the old object
    BOOL already_marked = marked (old_o);
    if (already_marked)
    {
        return nullptr;
    }
    set_marked (old_o);
    return old_o;
'@
            $c14QueueReplacement = @'
#ifdef MARK_PHASE_PREFETCH
    Prefetch (o);
    size_t slot_index = curr_slot_index;
    uint8_t* old_o = slot_table[slot_index];
    guideXosNativeAotFirstRootNonNullOldOWorklistWriteBefore(
        reinterpret_cast<uintptr_t>(o),
        reinterpret_cast<uintptr_t>(&slot_table[slot_index]),
        reinterpret_cast<uintptr_t>(old_o),
        static_cast<uintptr_t>(slot_index),
        static_cast<uintptr_t>(curr_slot_index),
        reinterpret_cast<uintptr_t>(slot_table),
        static_cast<uint32_t>(slot_count));
    slot_table[slot_index] = o;
    curr_slot_index = (slot_index + 1) % slot_count;
    guideXosNativeAotFirstRootNonNullOldOWorklistWriteCompleted(
        reinterpret_cast<uintptr_t>(o),
        reinterpret_cast<uintptr_t>(&slot_table[slot_index]),
        reinterpret_cast<uintptr_t>(old_o),
        reinterpret_cast<uintptr_t>(o),
        static_cast<uintptr_t>(slot_index),
        static_cast<uintptr_t>(curr_slot_index),
        reinterpret_cast<uintptr_t>(slot_table),
        static_cast<uint32_t>(slot_count));
    guideXosNativeAotFirstRootNonNullOldODecisionRequested(
        reinterpret_cast<uintptr_t>(o),
        reinterpret_cast<uintptr_t>(&slot_table[slot_index]),
        reinterpret_cast<uintptr_t>(old_o),
        reinterpret_cast<uintptr_t>(o),
        static_cast<uintptr_t>(slot_index),
        static_cast<uintptr_t>(slot_index),
        static_cast<uintptr_t>(curr_slot_index),
        reinterpret_cast<uintptr_t>(slot_table),
        static_cast<uint32_t>(slot_count));
    guideXosNativeAotFirstRootNonNullOldODecisionEntered(
        reinterpret_cast<uintptr_t>(old_o));
    if (old_o == nullptr)
    {
        guideXosNativeAotFirstRootNonNullOldONullDecisionCompleted(
            reinterpret_cast<uintptr_t>(old_o));
        C011EC15_QUEUE_MARK_RETURN_HOOK
        return nullptr;
    }
    guideXosNativeAotFirstRootNonNullOldONonNullDecisionStarted(
        reinterpret_cast<uintptr_t>(old_o));
    guideXosNativeAotFirstRootNonNullOldOMarkedRequested(
        reinterpret_cast<uintptr_t>(old_o));
    BOOL already_marked = marked (old_o);
    guideXosNativeAotFirstRootNonNullOldOMarkedCompleted(
        reinterpret_cast<uintptr_t>(old_o), already_marked ? 1u : 0u);
    if (already_marked)
    {
        guideXosNativeAotFirstRootNonNullOldOMarkedTrueBoundary(
            reinterpret_cast<uintptr_t>(old_o));
        return nullptr;
    }
    guideXosNativeAotFirstRootNonNullOldOMarkedFalseBoundary(
        reinterpret_cast<uintptr_t>(old_o));
    set_marked (old_o);
    return old_o;
#else //MARK_PHASE_PREFETCH
    uint8_t* old_o = o;
    guideXosNativeAotFirstRootNonNullOldODecisionEntered(
        reinterpret_cast<uintptr_t>(old_o));
    guideXosNativeAotFirstRootNonNullOldONonNullDecisionStarted(
        reinterpret_cast<uintptr_t>(old_o));
    guideXosNativeAotFirstRootNonNullOldOMarkedRequested(
        reinterpret_cast<uintptr_t>(old_o));
    BOOL already_marked = marked (old_o);
    guideXosNativeAotFirstRootNonNullOldOMarkedCompleted(
        reinterpret_cast<uintptr_t>(old_o), already_marked ? 1u : 0u);
    if (already_marked)
    {
        guideXosNativeAotFirstRootNonNullOldOMarkedTrueBoundary(
            reinterpret_cast<uintptr_t>(old_o));
        return nullptr;
    }
    guideXosNativeAotFirstRootNonNullOldOMarkedFalseBoundary(
        reinterpret_cast<uintptr_t>(old_o));
    set_marked (old_o);
    return old_o;
#endif //MARK_PHASE_PREFETCH
'@
            if ($isNextGenuineRootProvider) {
                $c14QueueReplacement = $c14QueueReplacement.Replace(
                    '        C011EC15_QUEUE_MARK_RETURN_HOOK',
                    @'
        guideXosNativeAotC011EC15QueueMarkReturned(
            reinterpret_cast<uintptr_t>(o),
            reinterpret_cast<uintptr_t>(&slot_table[slot_index]),
            reinterpret_cast<uintptr_t>(old_o),
            reinterpret_cast<uintptr_t>(o),
            static_cast<uintptr_t>(slot_index),
            static_cast<uintptr_t>(slot_index),
            static_cast<uintptr_t>(curr_slot_index),
            reinterpret_cast<uintptr_t>(slot_table));
'@.TrimEnd())
            } else {
                $c14QueueReplacement = $c14QueueReplacement.Replace('        C011EC15_QUEUE_MARK_RETURN_HOOK' + [Environment]::NewLine, '')
            }
            if (-not $queueBody.Contains($c14QueueSequence)) { throw "Locked MARK_PHASE_PREFETCH queue_mark write sequence did not match C011EC14 instrumentation." }
            $queueBody = $queueBody.Replace($c14QueueSequence, $c14QueueReplacement.TrimEnd())
            $gcWksInjected = $queuePrefix + $queueBody + $gcWksInjected.Substring($queueEnd)

            $markReadSequence = @'
    BOOL IsMarked() const
    {
        return !!(((size_t)RawGetMethodTable()) & GC_MARKED);
    }
'@
            $markReadReplacement = @'
    BOOL IsMarked() const
    {
        const uintptr_t guideXosRawHeader =
            reinterpret_cast<uintptr_t>(RawGetMethodTable());
        guideXosNativeAotFirstRootNonNullOldORawMarkWordRead(
            reinterpret_cast<uintptr_t>(this),
            reinterpret_cast<uintptr_t>(this),
            guideXosRawHeader,
            static_cast<uintptr_t>(GC_MARKED));
        return !!(guideXosRawHeader & GC_MARKED);
    }
'@
            if (-not $gcWksInjected.Contains($markReadSequence)) { throw "Locked CObjectHeader::IsMarked source did not match C011EC14 instrumentation." }
            $gcWksInjected = $gcWksInjected.Replace($markReadSequence, $markReadReplacement.TrimEnd())
        }
        if ($gcWksInjected -eq $gcCppText -or
            (($isNextGenuineRootProvider -and $gcWksInjected -notmatch 'guideXosNativeAotC011EC15PromoteEntered') -or
             ($isC14QueueInstrumentation -and -not $isNextGenuineRootProvider -and $gcWksInjected -notmatch 'guideXosNativeAotFirstRootNonNullOldOCallbackEntered') -or
             (-not $isC14QueueInstrumentation -and $gcWksInjected -notmatch 'guideXosNativeAotFirstRootCallbackEntered')) -or
            $gcWksInjected -notmatch [regex]::Escape($candidateLoadedFunction) -or
            $gcWksInjected -notmatch 'reinterpret_cast<uintptr_t>\(o\)' -or
            ([regex]::Matches($gcWksInjected, ([regex]::Escape($candidateLoadedFunction) + '\(reinterpret_cast<uintptr_t>\(o\)\)')).Count -ne 1) -or
            ($isFirstRootMembershipClassification -and $gcWksInjected -notmatch 'guideXosNativeAotFirstRootMembershipResultBoundary\(1u\)') -or
            ($isFirstRootHeapResolutionOrCondemned -and (
                $gcWksInjected -notmatch 'guideXosNativeAotFirstRootHeapResolutionRequested' -or
                $gcWksInjected -notmatch 'guideXosNativeAotFirstRootHeapResolutionCompleted'))) {
            throw "Locked gc.cpp callback-entry injection did not match GCHeap::Promote and its first candidate load."
        }
        $gcWksInjected = $gcWksText.Replace('#include "gc.cpp"', $gcWksInjected)
        if ($gcWksInjected -eq $gcWksText -or $gcWksInjected -notmatch 'namespace WKS') {
            throw "Locked gcwks.cpp did not expose the Workstation GC source include for callback-entry replacement."
        }
        if ($isC011EC29) {
            $nextPhaseNeedle = '    GCScan::GcShortWeakPtrScan (condemned_gen_number, max_generation,&sc);'
            if (-not $gcWksInjected.Contains($nextPhaseNeedle)) {
                throw "Locked mark_phase short-weak scan statement was not found for C011EC29."
            }
            $nextPhaseReplacement = @'
    guideXosNativeAotC011EC29NextPhaseEntered(
        condemned_gen_number, max_generation,
        reinterpret_cast<uintptr_t>(&sc),
        static_cast<uint32_t>(settings.reason),
        settings.compaction != FALSE ? 1u : 0u,
        settings.promotion != FALSE ? 1u : 0u,
        reinterpret_cast<uintptr_t>(pGenGCHeap),
        static_cast<uint32_t>(total_generation_count),
        static_cast<uint32_t>(heap_number),
        full_p != FALSE ? 1u : 0u);
    GCScan::GcShortWeakPtrScan (condemned_gen_number, max_generation,&sc);
'@
            if ($isC011EC33) {
                $nextPhaseReplacement += [Environment]::NewLine + '    guideXosNativeAotC011EC33PostWeakPhase(1u);'
            }
            $gcWksInjected = $gcWksInjected.Replace($nextPhaseNeedle, $nextPhaseReplacement.TrimEnd())
        }
        if ($isC011EC29 -and $gcWksInjected -notmatch 'guideXosNativeAotC011EC29NextPhaseEntered') {
            throw "C011EC29 next-phase observer was not inserted into mark_phase."
        }
        if ($isC011EC33) {
            $finalizationNeedle = '    finalize_queue->ScanForFinalization (GCHeap::Promote, condemned_gen_number, __this);'
            if (-not $gcWksInjected.Contains($finalizationNeedle)) {
                throw "C011EC33 finalization entry was not found after short-weak processing."
            }
            $finalizationReplacement = '    guideXosNativeAotC011EC33PostWeakPhase(2u);' + [Environment]::NewLine + $finalizationNeedle + [Environment]::NewLine + '    guideXosNativeAotC011EC33PostWeakPhase(3u);'
            $gcWksInjected = $gcWksInjected.Replace($finalizationNeedle, $finalizationReplacement)

            $longWeakNeedle = '    GCScan::GcWeakPtrScan (condemned_gen_number, max_generation, &sc);'
            if (-not $gcWksInjected.Contains($longWeakNeedle)) {
                throw "C011EC33 long-weak entry was not found after short-weak processing."
            }
            $longWeakReplacement = '    guideXosNativeAotC011EC33PostWeakPhase(4u);' + [Environment]::NewLine + $longWeakNeedle + [Environment]::NewLine + '    guideXosNativeAotC011EC33PostWeakPhase(5u);'
            $gcWksInjected = $gcWksInjected.Replace($longWeakNeedle, $longWeakReplacement)

            $syncBlockNeedle = '        GCScan::GcWeakPtrScanBySingleThread(condemned_gen_number, max_generation, &sc);'
            if (-not $gcWksInjected.Contains($syncBlockNeedle)) {
                throw "C011EC33 sync-block weak scan entry was not found after short-weak processing."
            }
            $syncBlockReplacement = '        guideXosNativeAotC011EC33PostWeakPhase(6u);' + [Environment]::NewLine + $syncBlockNeedle + [Environment]::NewLine + '        guideXosNativeAotC011EC33PostWeakPhase(7u);'
            $gcWksInjected = $gcWksInjected.Replace($syncBlockNeedle, $syncBlockReplacement)

            $planNeedle = '            plan_phase (n);'
            if (-not $gcWksInjected.Contains($planNeedle)) {
                throw "C011EC33 plan entry was not found after short-weak processing."
            }
            $planReplacement = '            guideXosNativeAotC011EC33PostWeakPhase(8u);' + [Environment]::NewLine + $planNeedle + [Environment]::NewLine + '            guideXosNativeAotC011EC33PostWeakPhase(9u);'
            $gcWksInjected = $gcWksInjected.Replace($planNeedle, $planReplacement)

            $relocateNeedle = '        relocate_phase (condemned_gen_number, first_condemned_address);'
            $compactNeedle = '        compact_phase (condemned_gen_number, first_condemned_address,'
            if (-not $gcWksInjected.Contains($relocateNeedle) -or -not $gcWksInjected.Contains($compactNeedle)) {
                throw "C011EC33 relocation/compaction entries were not found after short-weak processing."
            }
            $gcWksInjected = $gcWksInjected.Replace($relocateNeedle, '        guideXosNativeAotC011EC33PostWeakPhase(10u);' + [Environment]::NewLine + $relocateNeedle + [Environment]::NewLine + '        guideXosNativeAotC011EC33PostWeakPhase(11u);')
            $gcWksInjected = $gcWksInjected.Replace($compactNeedle, '        guideXosNativeAotC011EC33PostWeakPhase(12u);' + [Environment]::NewLine + $compactNeedle)

            $relocateRootsNeedle = '    GCScan::GcScanRoots(GCHeap::Relocate,'
            if (-not $gcWksInjected.Contains($relocateRootsNeedle)) {
                throw "C011EC33 relocation root scan entry was not found."
            }
            $relocateRootsReplacement = '    guideXosNativeAotC011EC33PostWeakPhase(13u);' + [Environment]::NewLine + $relocateRootsNeedle
            if ($isC011EC34) {
                $relocateRootsReplacement = '    guideXosNativeAotC011EC33PostWeakPhase(13u);' + [Environment]::NewLine + '    guideXosNativeAotC011EC34RelocationPhaseEntered(static_cast<uint32_t>(condemned_gen_number), settings.compaction ? 1u : 0u);' + [Environment]::NewLine + $relocateRootsNeedle
            }
            $gcWksInjected = $gcWksInjected.Replace($relocateRootsNeedle, $relocateRootsReplacement)
            $relocateRootsReturnNeedle = '    verify_pins_with_post_plug_info("after reloc stack");'
            if (-not $gcWksInjected.Contains($relocateRootsReturnNeedle)) {
                throw "C011EC33 relocation root scan return was not found."
            }
            $relocateRootsReturnReplacement = '    guideXosNativeAotC011EC33PostWeakPhase(14u);' + [Environment]::NewLine + $relocateRootsReturnNeedle
            if ($isC011EC34) {
                $relocateRootsReturnReplacement = '    guideXosNativeAotC011EC33PostWeakPhase(14u);' + [Environment]::NewLine + '    guideXosNativeAotC011EC34RelocationRootScanReturned();' + [Environment]::NewLine + $relocateRootsReturnNeedle
            }
            $gcWksInjected = $gcWksInjected.Replace($relocateRootsReturnNeedle, $relocateRootsReturnReplacement)

            $relocateHandlesNeedle = '        GCScan::GcScanHandles(GCHeap::Relocate,'
            if (-not $gcWksInjected.Contains($relocateHandlesNeedle)) {
                throw "C011EC33 relocation handle scan entry was not found."
            }
            $relocateHandlesReplacement = '        guideXosNativeAotC011EC33PostWeakPhase(15u);' + [Environment]::NewLine + $relocateHandlesNeedle
            $gcWksInjected = $gcWksInjected.Replace($relocateHandlesNeedle, $relocateHandlesReplacement)
        }
        Set-Content -LiteralPath $gcWksSource -Value $gcWksInjected -Encoding ASCII
    }
    if ($isCandidateLoadEnumeration) {
        $lockedGcEnumPath = Join-Path $lockedSourceRoot "src\coreclr\nativeaot\Runtime\GcEnum.cpp"
        Require-File $lockedGcEnumPath "Locked NativeAOT GcEnum source"
        $gcEnumText = Get-Content -LiteralPath $lockedGcEnumPath -Raw
        if ($isNextGenuineRootProvider) {
            $gcEnumDeclaration = @(
                'extern "C" void __cdecl guideXosNativeAotC011EC15CandidateObserved(uintptr_t slot, uintptr_t rawValue, uint32_t flags, uintptr_t callback, uintptr_t context);',
                'extern "C" void __cdecl guideXosNativeAotC011EC15EnumGcRefReturned(uintptr_t slot, uintptr_t rawValue, uintptr_t callback, uintptr_t context);'
            ) -join [Environment]::NewLine
            if ($isC011EC19 -or $isC011EC34) {
                $gcEnumDeclaration += [Environment]::NewLine + 'extern "C" void __cdecl guideXosNativeAotC011EC19GcRootReported(uintptr_t slot, uint32_t flags, uint32_t rootKind, uintptr_t registerSlot);'
            }
            if ($isC011EC31) {
                $gcEnumDeclaration += [Environment]::NewLine + 'extern "C" void __cdecl guideXosNativeAotC011EC31StrongRootCandidate(uintptr_t slot, uintptr_t rawValue);'
            }
            if ($isC011EC32) {
                $gcEnumDeclaration += [Environment]::NewLine + 'extern "C" void __cdecl guideXosNativeAotC011EC32StrongRootCandidate(uintptr_t slot, uintptr_t rawValue);'
                $gcEnumDeclaration += [Environment]::NewLine + 'extern "C" void __cdecl guideXosNativeAotC011EC32GcRootReported(uintptr_t slot, uint32_t flags, uint32_t rootKind, uintptr_t registerSlot);'
            }
            if ($isC011EC33) {
                $gcEnumDeclaration += [Environment]::NewLine + 'extern "C" void __cdecl guideXosNativeAotC011EC33GcRootReported(uintptr_t slot, uint32_t flags, uint32_t rootKind, uintptr_t registerSlot);'
                if ($isC011EC34) {
                    $gcEnumDeclaration += [Environment]::NewLine + 'extern "C" void __cdecl guideXosNativeAotC011EC34GcRootReported(uintptr_t slot, uint32_t flags, uint32_t rootKind, uintptr_t registerSlot, uintptr_t callback, uintptr_t context);'
                }
            }
        } elseif ($isFirstRootFirstNonNullOldO) {
            $gcEnumDeclaration = @(
                'extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOCandidateLoadRequested(uintptr_t slot, uint32_t flags, uintptr_t callback, uintptr_t scanContext);',
                'extern "C" uint32_t __cdecl guideXosNativeAotFirstRootNonNullOldOCandidateMachineWordLoaded(uintptr_t slot, uintptr_t rawValue);',
                'extern "C" void __cdecl guideXosNativeAotFirstRootCallbackCallSiteEntered(uintptr_t slot, uintptr_t rawValue, uint32_t flags, uintptr_t callback, uintptr_t scanContext);'
            ) -join [Environment]::NewLine
        } elseif ($isFirstRootCallbackEntry) {
            $gcEnumDeclaration = @(
                'extern "C" void __cdecl guideXosNativeAotFirstNonNullRootCandidateLoadRequested(uintptr_t slot, uint32_t flags, uintptr_t callback, uintptr_t scanContext);',
                'extern "C" uint32_t __cdecl guideXosNativeAotFirstNonNullRootCandidateMachineWordLoaded(uintptr_t slot, uintptr_t rawValue);',
                'extern "C" void __cdecl guideXosNativeAotFirstRootCallbackCallSiteEntered(uintptr_t slot, uintptr_t rawValue, uint32_t flags, uintptr_t callback, uintptr_t scanContext);'
            ) -join [Environment]::NewLine
        } elseif ($isFirstNonNullRoot) {
            $gcEnumDeclaration = @(
                'extern "C" void __cdecl guideXosNativeAotFirstNonNullRootCandidateLoadRequested(uintptr_t slot, uint32_t flags, uintptr_t callback, uintptr_t scanContext);',
                'extern "C" uint32_t __cdecl guideXosNativeAotFirstNonNullRootCandidateMachineWordLoaded(uintptr_t slot, uintptr_t rawValue);'
            ) -join [Environment]::NewLine
        } else {
            $gcEnumDeclaration = @(
                'extern "C" void __cdecl guideXosNativeAotFirstRootCandidateLoadRequested(uintptr_t slot, uint32_t flags, uintptr_t callback, uintptr_t scanContext);',
                'extern "C" __declspec(noreturn) void __cdecl guideXosNativeAotFirstRootCandidateMachineWordLoaded(uintptr_t slot, uintptr_t rawValue);'
            ) -join [Environment]::NewLine
        }
        if ($isNextGenuineRootProvider) {
            $gcEnumText = $gcEnumText.Replace('#include "GcEnum.h"', '#include "GcEnum.h"' + [Environment]::NewLine + [Environment]::NewLine + $gcEnumDeclaration.TrimEnd())
            $gcEnumPattern = '(?ms)^static void GcEnumObject\(PTR_PTR_Object ppObj, uint32_t flags, ScanFunc\* fnGcEnumRef, ScanContext\* pSc\)\r?\n\{.*?\r?\n\}\r?\n\r?\n(?=void EnumGcRef)'
            $gcEnumReplacement = @'
static void GcEnumObject(PTR_PTR_Object ppObj, uint32_t flags, ScanFunc* fnGcEnumRef, ScanContext* pSc)
{
    const uintptr_t candidateRawValue = reinterpret_cast<uintptr_t>(*ppObj);
    guideXosNativeAotC011EC15CandidateObserved(
        reinterpret_cast<uintptr_t>(ppObj), candidateRawValue, flags,
        reinterpret_cast<uintptr_t>(fnGcEnumRef),
        reinterpret_cast<uintptr_t>(pSc));
    if (flags & GC_CALL_INTERIOR)
        PromoteCarefully(ppObj, flags, fnGcEnumRef, pSc);
    else
        fnGcEnumRef(ppObj, pSc, flags);
    guideXosNativeAotC011EC15EnumGcRefReturned(
        reinterpret_cast<uintptr_t>(ppObj), candidateRawValue,
        reinterpret_cast<uintptr_t>(fnGcEnumRef),
        reinterpret_cast<uintptr_t>(pSc));
}
'@
            if ($isC011EC31) {
                $gcEnumReplacement = $gcEnumReplacement.Replace(
                    '    const uintptr_t candidateRawValue = reinterpret_cast<uintptr_t>(*ppObj);',
                    '    const uintptr_t candidateRawValue = reinterpret_cast<uintptr_t>(*ppObj);' + [Environment]::NewLine + '    guideXosNativeAotC011EC31StrongRootCandidate(' + [Environment]::NewLine + '        reinterpret_cast<uintptr_t>(ppObj), candidateRawValue);')
            }
            if ($isC011EC32) {
                $gcEnumReplacement = $gcEnumReplacement.Replace(
                    '    const uintptr_t candidateRawValue = reinterpret_cast<uintptr_t>(*ppObj);',
                    '    const uintptr_t candidateRawValue = reinterpret_cast<uintptr_t>(*ppObj);' + [Environment]::NewLine + '    guideXosNativeAotC011EC32StrongRootCandidate(' + [Environment]::NewLine + '        reinterpret_cast<uintptr_t>(ppObj), candidateRawValue);')
            }
            $gcEnumInjected = [regex]::Replace($gcEnumText, $gcEnumPattern, $gcEnumReplacement.TrimEnd() + [Environment]::NewLine + [Environment]::NewLine, 1)
            $gcEnumInjected = $gcEnumInjected.Replace(
                '    GcEnumObject(pRef, flags, fnGcEnumRef, pSc);',
                '    GcEnumObject(pRef, flags, fnGcEnumRef, pSc);' + [Environment]::NewLine + '    guideXosNativeAotC011EC15EnumGcRefReturned(0u, 0u, 0u, 0u);')
            if ($isC011EC19 -or $isC011EC34) {
                $contextPattern = '(?ms)(struct EnumGcRefContext : GCEnumContext\s*\{\s*ScanFunc\* f;\s*ScanContext\* sc;)'
                $contextReplacement = '$1' + [Environment]::NewLine + '    REGDISPLAY* pRegisterSet;'
                $gcEnumInjected = [regex]::Replace($gcEnumInjected, $contextPattern, $contextReplacement, 1)
                $callbackPattern = '(?ms)static void EnumGcRefsCallback\(void\* hCallback, PTR_PTR_VOID pObject, uint32_t flags\)\s*\{\s*EnumGcRefContext\* pCtx = \(EnumGcRefContext\*\)hCallback;\s*GcEnumObject\(\(PTR_OBJECTREF\)pObject, flags, pCtx->f, pCtx->sc\);\s*\}'
                $callbackReplacement = @'
static void EnumGcRefsCallback(void* hCallback, PTR_PTR_VOID pObject, uint32_t flags)
{
    EnumGcRefContext* pCtx = (EnumGcRefContext*)hCallback;
    uint32_t rootKind = 2u;
    uintptr_t registerSlot = 0u;
    if (pCtx->pRegisterSet != nullptr)
    {
        if (reinterpret_cast<uintptr_t>(pObject) == reinterpret_cast<uintptr_t>(pCtx->pRegisterSet->pRbx)) { rootKind = 1u; registerSlot = reinterpret_cast<uintptr_t>(pCtx->pRegisterSet->pRbx); }
        else if (reinterpret_cast<uintptr_t>(pObject) == reinterpret_cast<uintptr_t>(pCtx->pRegisterSet->pRbp)) { rootKind = 1u; registerSlot = reinterpret_cast<uintptr_t>(pCtx->pRegisterSet->pRbp); }
        else if (reinterpret_cast<uintptr_t>(pObject) == reinterpret_cast<uintptr_t>(pCtx->pRegisterSet->pRsi)) { rootKind = 1u; registerSlot = reinterpret_cast<uintptr_t>(pCtx->pRegisterSet->pRsi); }
        else if (reinterpret_cast<uintptr_t>(pObject) == reinterpret_cast<uintptr_t>(pCtx->pRegisterSet->pRdi)) { rootKind = 1u; registerSlot = reinterpret_cast<uintptr_t>(pCtx->pRegisterSet->pRdi); }
        else if (reinterpret_cast<uintptr_t>(pObject) == reinterpret_cast<uintptr_t>(pCtx->pRegisterSet->pR12)) { rootKind = 1u; registerSlot = reinterpret_cast<uintptr_t>(pCtx->pRegisterSet->pR12); }
        else if (reinterpret_cast<uintptr_t>(pObject) == reinterpret_cast<uintptr_t>(pCtx->pRegisterSet->pR13)) { rootKind = 1u; registerSlot = reinterpret_cast<uintptr_t>(pCtx->pRegisterSet->pR13); }
        else if (reinterpret_cast<uintptr_t>(pObject) == reinterpret_cast<uintptr_t>(pCtx->pRegisterSet->pR14)) { rootKind = 1u; registerSlot = reinterpret_cast<uintptr_t>(pCtx->pRegisterSet->pR14); }
        else if (reinterpret_cast<uintptr_t>(pObject) == reinterpret_cast<uintptr_t>(pCtx->pRegisterSet->pR15)) { rootKind = 1u; registerSlot = reinterpret_cast<uintptr_t>(pCtx->pRegisterSet->pR15); }
    }
    C011EC19_ROOT_REPORT
    C011EC32_ROOT_REPORT
    GcEnumObject((PTR_OBJECTREF)pObject, flags, pCtx->f, pCtx->sc);
}
'@
                if ($isC011EC19) {
                    $callbackReplacement = $callbackReplacement.Replace(
                        '    C011EC19_ROOT_REPORT',
                        '    guideXosNativeAotC011EC19GcRootReported(' + [Environment]::NewLine +
                        '        reinterpret_cast<uintptr_t>(pObject), flags, rootKind, registerSlot);')
                } else {
                    $callbackReplacement = $callbackReplacement.Replace('    C011EC19_ROOT_REPORT' + [Environment]::NewLine, '')
                }
                if ($isC011EC32) {
                    $callbackReplacement = $callbackReplacement.Replace(
                        '    C011EC32_ROOT_REPORT',
                        '    guideXosNativeAotC011EC32GcRootReported(' + [Environment]::NewLine +
                        '        reinterpret_cast<uintptr_t>(pObject), flags, rootKind, registerSlot);')
                } else {
                    $callbackReplacement = $callbackReplacement.Replace('    C011EC32_ROOT_REPORT' + [Environment]::NewLine, '')
                }
                if ($isC011EC33) {
                    $callbackReplacement = $callbackReplacement.Replace(
                        '    guideXosNativeAotC011EC32GcRootReported(' + [Environment]::NewLine +
                        '        reinterpret_cast<uintptr_t>(pObject), flags, rootKind, registerSlot);',
                        '    guideXosNativeAotC011EC32GcRootReported(' + [Environment]::NewLine +
                        '        reinterpret_cast<uintptr_t>(pObject), flags, rootKind, registerSlot);' + [Environment]::NewLine +
                        '    guideXosNativeAotC011EC33GcRootReported(' + [Environment]::NewLine +
                        '        reinterpret_cast<uintptr_t>(pObject), flags, rootKind, registerSlot);')
                    if ($isC011EC34) {
                        $callbackReplacement = $callbackReplacement.Replace(
                            '    guideXosNativeAotC011EC33GcRootReported(' + [Environment]::NewLine +
                            '        reinterpret_cast<uintptr_t>(pObject), flags, rootKind, registerSlot);',
                            '    guideXosNativeAotC011EC33GcRootReported(' + [Environment]::NewLine +
                            '        reinterpret_cast<uintptr_t>(pObject), flags, rootKind, registerSlot);' + [Environment]::NewLine +
                            '    guideXosNativeAotC011EC34GcRootReported(' + [Environment]::NewLine +
                            '        reinterpret_cast<uintptr_t>(pObject), flags, rootKind, registerSlot,' + [Environment]::NewLine +
                            '        reinterpret_cast<uintptr_t>(pCtx->f), reinterpret_cast<uintptr_t>(pCtx->sc));')
                    }
                }
                $gcEnumInjected = [regex]::Replace($gcEnumInjected, $callbackPattern, $callbackReplacement.TrimEnd(), 1)
                $gcEnumInjected = $gcEnumInjected.Replace(
                    '    ctx.sc = pvCallbackData;',
                    '    ctx.sc = pvCallbackData;' + [Environment]::NewLine + '    ctx.pRegisterSet = pRegisterSet;')
            }
            $gcEnumInjectionValid =
                $gcEnumInjected -ne $gcEnumText -and
                $gcEnumInjected -match 'guideXosNativeAotC011EC15CandidateObserved' -and
                $gcEnumInjected -match 'guideXosNativeAotC011EC15EnumGcRefReturned'
        } elseif ($isFirstRootFirstNonNullOldO) {
            # Keep GcEnum.cpp byte-for-byte source behavior for C011EC14. The
            # proof boundary is in GCHeap::Promote and queue_mark; this source
            # is compiled into the fresh pack without changing callback flow.
            $gcEnumInjected = $gcEnumText
            $gcEnumInjectionValid = $true
        } else {
            $gcEnumText = $gcEnumText.Replace('#include "GcEnum.h"', '#include "GcEnum.h"' + [Environment]::NewLine + [Environment]::NewLine + $gcEnumDeclaration.TrimEnd())
            $gcEnumPattern = '(?m)^static void GcEnumObject\(PTR_PTR_Object ppObj, uint32_t flags, ScanFunc\* fnGcEnumRef, ScanContext\* pSc\)\r?\n\{'
        if ($isFirstNonNullRoot -or $isFirstRootCallbackEntry) {
            $candidateRequestFunction = if ($isFirstRootFirstNonNullOldO) { 'guideXosNativeAotFirstRootNonNullOldOCandidateLoadRequested' } else { 'guideXosNativeAotFirstNonNullRootCandidateLoadRequested' }
            $candidateWordFunction = if ($isFirstRootFirstNonNullOldO) { 'guideXosNativeAotFirstRootNonNullOldOCandidateMachineWordLoaded' } else { 'guideXosNativeAotFirstNonNullRootCandidateMachineWordLoaded' }
            if ($isFirstRootFirstNonNullOldO) {
                $gcEnumReplacement = @(
                    'static void GcEnumObject(PTR_PTR_Object ppObj, uint32_t flags, ScanFunc* fnGcEnumRef, ScanContext* pSc)',
                    '{',
                    "    $candidateRequestFunction(",
                    '        reinterpret_cast<uintptr_t>(ppObj), flags,',
                    '        reinterpret_cast<uintptr_t>(fnGcEnumRef),',
                    '        reinterpret_cast<uintptr_t>(pSc));',
                    '    const uintptr_t candidateRawValue = reinterpret_cast<uintptr_t>(*ppObj);',
                    "    (void)$candidateWordFunction(",
                    '        reinterpret_cast<uintptr_t>(ppObj), candidateRawValue);'
                ) -join [Environment]::NewLine
            } else {
                $gcEnumReplacement = @(
                    'static void GcEnumObject(PTR_PTR_Object ppObj, uint32_t flags, ScanFunc* fnGcEnumRef, ScanContext* pSc)',
                    '{',
                    "    $candidateRequestFunction(",
                    '        reinterpret_cast<uintptr_t>(ppObj), flags,',
                    '        reinterpret_cast<uintptr_t>(fnGcEnumRef),',
                    '        reinterpret_cast<uintptr_t>(pSc));',
                    '    const uintptr_t candidateRawValue = reinterpret_cast<uintptr_t>(*ppObj);',
                    "    if ($candidateWordFunction(",
                    '            reinterpret_cast<uintptr_t>(ppObj), candidateRawValue) == 0u)',
                    '        return;'
                ) -join [Environment]::NewLine
                if ($isFirstRootCallbackEntry) {
                    $gcEnumReplacement += [Environment]::NewLine + ((@(
                        '    guideXosNativeAotFirstRootCallbackCallSiteEntered(',
                        '        reinterpret_cast<uintptr_t>(ppObj), candidateRawValue, flags,',
                        '        reinterpret_cast<uintptr_t>(fnGcEnumRef),',
                        '        reinterpret_cast<uintptr_t>(pSc));'
                    ) -join [Environment]::NewLine))
                }
            }
        } else {
            $gcEnumReplacement = @(
                'static void GcEnumObject(PTR_PTR_Object ppObj, uint32_t flags, ScanFunc* fnGcEnumRef, ScanContext* pSc)',
                '{',
                '    guideXosNativeAotFirstRootCandidateLoadRequested(',
                '        reinterpret_cast<uintptr_t>(ppObj), flags,',
                '        reinterpret_cast<uintptr_t>(fnGcEnumRef),',
                '        reinterpret_cast<uintptr_t>(pSc));',
                '    const uintptr_t candidateRawValue = reinterpret_cast<uintptr_t>(*ppObj);',
                '    guideXosNativeAotFirstRootCandidateMachineWordLoaded(',
                '        reinterpret_cast<uintptr_t>(ppObj), candidateRawValue);'
            ) -join [Environment]::NewLine
        }
            $gcEnumInjected = [regex]::Replace($gcEnumText, $gcEnumPattern, $gcEnumReplacement.TrimEnd(), 1)
            $gcEnumInjectionValid =
                $gcEnumInjected -ne $gcEnumText -and
                $gcEnumInjected -match 'const uintptr_t candidateRawValue = reinterpret_cast<uintptr_t>\(\*ppObj\);'
            if ($isFirstNonNullRoot -or $isFirstRootCallbackEntry) {
                $gcEnumInjectionValid = $gcEnumInjectionValid -and
                    $gcEnumInjected -match [regex]::Escape($candidateWordFunction)
            } else {
                $gcEnumInjectionValid = $gcEnumInjectionValid -and
                    $gcEnumInjected -match 'guideXosNativeAotFirstRootCandidateMachineWordLoaded'
            }
            if (-not $gcEnumInjectionValid) {
                throw "Locked GcEnum.cpp first-root-candidate-load injection did not match the GcEnumObject load boundary."
            }
        }
        Set-Content -LiteralPath $gcEnumSource -Value $gcEnumInjected -Encoding ASCII
    }
    if ($isC011EC30) {
        $lockedObjectHandlePath = Join-Path $lockedSourceRoot "src\coreclr\gc\objecthandle.cpp"
        Require-File $lockedObjectHandlePath "Locked NativeAOT objecthandle source"
        $objectHandleText = Get-Content -LiteralPath $lockedObjectHandlePath -Raw
        $decisionCompletedAttribute = if ($isC011EC33) { '' } else { '__declspec(noreturn) ' }
        $objectHandleDeclaration = @(
            'extern "C" void __cdecl guideXosNativeAotC011EC29HandleScanEntered(uint32_t condemned, uint32_t maxGeneration, uintptr_t scanContext);',
            'extern "C" void __cdecl guideXosNativeAotC011EC30HandleScanEntered(uint32_t condemned, uint32_t maxGeneration, uintptr_t scanContext);',
            'extern "C" void __cdecl guideXosNativeAotC011EC30HandleMapRootRead(uintptr_t mapAddress, uintptr_t bucketsFieldAddress, uintptr_t bucketsValue, uintptr_t maxIndex, uint32_t flags);',
            'extern "C" void __cdecl guideXosNativeAotC011EC30BucketVisited(uint32_t bucketIndex, uintptr_t bucketAddress, uintptr_t tableArray, uint32_t tableIndex);',
            'extern "C" void __cdecl guideXosNativeAotC011EC30HandleTableVisited(uintptr_t tableAddress, uint32_t bucketIndex, uint32_t cpuIndex);',
            ('extern "C" ' + $(if ($isC011EC33) { '' } else { '__declspec(noreturn) ' }) + 'void __cdecl guideXosNativeAotC011EC30HandleScanCompleted(uint32_t condemned, uint32_t maxGeneration);'),
            'extern "C" void __cdecl guideXosNativeAotC011EC30HandleCallbackExpected(uintptr_t callbackAddress);',
            'extern "C" void __cdecl guideXosNativeAotC011EC30ProductionCallbackEntered();',
            'extern "C" void __cdecl guideXosNativeAotC011EC30LivenessCheckEntered(uintptr_t slotAddress, uintptr_t target);',
            'extern "C" void __cdecl guideXosNativeAotC011EC30LivenessDecisionObserved(uint32_t promoted);',
            ('extern "C" ' + $decisionCompletedAttribute + 'void __cdecl guideXosNativeAotC011EC30LivenessDecisionCompleted(uintptr_t slotAddress, uintptr_t before, uintptr_t after);')
        ) -join [Environment]::NewLine
        if ($isC011EC31) {
            $objectHandleDeclaration += [Environment]::NewLine + 'extern "C" void __cdecl guideXosNativeAotC011EC31StrongHandlePromoted(uintptr_t slotAddress, uintptr_t target);'
            $objectHandleDeclaration += [Environment]::NewLine + 'extern "C" void __cdecl guideXosNativeAotC011EC31LivenessCheckEntered(uintptr_t slotAddress, uintptr_t target, uint32_t targetGeneration, uintptr_t markWordAddress, uintptr_t markWordBefore, uintptr_t markMask);'
        }
        if ($isC011EC32) {
            $objectHandleDeclaration += [Environment]::NewLine + 'extern "C" void __cdecl guideXosNativeAotC011EC32StrongHandlePromoted(uintptr_t slotAddress, uintptr_t target);'
            $objectHandleDeclaration += [Environment]::NewLine + 'extern "C" void __cdecl guideXosNativeAotC011EC32LivenessCheckEntered(uintptr_t slotAddress, uintptr_t target, uint32_t targetGeneration, uintptr_t markWordAddress, uintptr_t markWordBefore, uintptr_t markMask);'
            $objectHandleDeclaration += [Environment]::NewLine + 'extern "C" void __cdecl guideXosNativeAotC011EC32ClearingStoreEntered(uintptr_t slotAddress, uintptr_t before);'
        }
        if ($isC011EC33) {
            $objectHandleDeclaration += [Environment]::NewLine + 'extern "C" void __cdecl guideXosNativeAotC011EC33LivenessCheckEntered(uintptr_t slotAddress, uintptr_t target, uintptr_t targetGeneration, uintptr_t markWordAddress, uintptr_t markWordBefore, uintptr_t markMask);'
            $objectHandleDeclaration += [Environment]::NewLine + 'extern "C" void __cdecl guideXosNativeAotC011EC33ClearingStoreEntered(uintptr_t slotAddress, uintptr_t before);'
            $objectHandleDeclaration += [Environment]::NewLine + 'extern "C" void __cdecl guideXosNativeAotC011EC33LivenessDecisionObserved(uint32_t promoted);'
        }
        $objectHandleText = $objectHandleText.Replace(
            '#include "objecthandle.h"',
            '#include "objecthandle.h"' + [Environment]::NewLine + [Environment]::NewLine + $objectHandleDeclaration)
        $refPattern = '(?m)^void Ref_CheckAlive\(uint32_t condemned, uint32_t maxgen, ScanContext \*sc\)\r?\n\{'
        $refReplacement = 'void Ref_CheckAlive(uint32_t condemned, uint32_t maxgen, ScanContext *sc)' + [Environment]::NewLine + '{' + [Environment]::NewLine + '    guideXosNativeAotC011EC29HandleScanEntered(condemned, maxgen, reinterpret_cast<uintptr_t>(sc));' + [Environment]::NewLine + '    guideXosNativeAotC011EC30HandleScanEntered(condemned, maxgen, reinterpret_cast<uintptr_t>(sc));'
        $objectHandleText = [regex]::Replace($objectHandleText, $refPattern, $refReplacement, 1)
        $callbackEntryPattern = '(?m)^void CALLBACK CheckPromoted\([^\r\n]+\r?\n\{'
        $callbackEntryReplacement = '$0' + [Environment]::NewLine + '    guideXosNativeAotC011EC30ProductionCallbackEntered();'
        $objectHandleText = [regex]::Replace($objectHandleText, $callbackEntryPattern, $callbackEntryReplacement, 1)
        if ($objectHandleText -notmatch 'guideXosNativeAotC011EC30ProductionCallbackEntered\(\);') { throw "Locked CheckPromoted callback entry was not instrumented for C011EC30." }
        if ($isC011EC31 -or $isC011EC32) {
            $strongPromotePattern = '(?m)^void CALLBACK PromoteObject\([^\r\n]+\r?\n\{'
            $strongPromoteFunction = if ($isC011EC32) { 'guideXosNativeAotC011EC32StrongHandlePromoted' } else { 'guideXosNativeAotC011EC31StrongHandlePromoted' }
            $strongPromoteReplacement = '$0' + [Environment]::NewLine + '    ' + $strongPromoteFunction + '(' + [Environment]::NewLine + '        reinterpret_cast<uintptr_t>(pObjRef), reinterpret_cast<uintptr_t>(*pObjRef));'
            $objectHandleText = [regex]::Replace($objectHandleText, $strongPromotePattern, $strongPromoteReplacement, 1)
            $strongPromoteName = if ($isC011EC32) { 'guideXosNativeAotC011EC32StrongHandlePromoted' } else { 'guideXosNativeAotC011EC31StrongHandlePromoted' }
            if ($objectHandleText -notmatch [regex]::Escape($strongPromoteName + '(')) { throw "$strongPromoteName strong-handle PromoteObject callback was not instrumented." }
        }
        $refStart = $objectHandleText.IndexOf('void Ref_CheckAlive(', [System.StringComparison]::Ordinal)
        if ($refStart -lt 0) { throw "Locked Ref_CheckAlive definition was not found for C011EC30." }
        $mapNeedle = '    HandleTableMap *walk = &g_HandleTableMap;'
        $mapOffset = $objectHandleText.IndexOf($mapNeedle, $refStart, [System.StringComparison]::Ordinal)
        if ($mapOffset -lt 0) { throw "Locked Ref_CheckAlive HandleTableMap walk was not found for C011EC30." }
        $mapReplacement = '    HandleTableMap *walk = &g_HandleTableMap;' + [Environment]::NewLine + '    guideXosNativeAotC011EC30HandleMapRootRead(' + [Environment]::NewLine + '        reinterpret_cast<uintptr_t>(&g_HandleTableMap),' + [Environment]::NewLine + '        reinterpret_cast<uintptr_t>(&walk->pBuckets),' + [Environment]::NewLine + '        reinterpret_cast<uintptr_t>(walk->pBuckets),' + [Environment]::NewLine + '        static_cast<uintptr_t>(walk->dwMaxIndex),' + [Environment]::NewLine + '        flags);'
        $objectHandleText = $objectHandleText.Substring(0, $mapOffset) + $mapReplacement + $objectHandleText.Substring($mapOffset + $mapNeedle.Length)
        $bucketNeedle = '            if (walk->pBuckets[i] != NULL)' + [Environment]::NewLine + '            {'
        $bucketReplacement = '            if (walk->pBuckets[i] != NULL)' + [Environment]::NewLine + '            {' + [Environment]::NewLine + '                guideXosNativeAotC011EC30BucketVisited(' + [Environment]::NewLine + '                    i,' + [Environment]::NewLine + '                    reinterpret_cast<uintptr_t>(walk->pBuckets[i]),' + [Environment]::NewLine + '                    reinterpret_cast<uintptr_t>(walk->pBuckets[i]->pTable),' + [Environment]::NewLine + '                    static_cast<uint32_t>(walk->pBuckets[i]->HandleTableIndex));'
        $bucketOffset = $objectHandleText.IndexOf($bucketNeedle, $refStart, [System.StringComparison]::Ordinal)
        if ($bucketOffset -lt 0) { throw "Locked Ref_CheckAlive bucket branch was not found for C011EC30." }
        $objectHandleText = $objectHandleText.Substring(0, $bucketOffset) + $bucketReplacement + $objectHandleText.Substring($bucketOffset + $bucketNeedle.Length)
        $tableNeedle = '                    if (hTable)' + [Environment]::NewLine + '                        HndScanHandlesForGC(hTable, CheckPromoted, (uintptr_t)sc, 0, types, ARRAY_SIZE(types), condemned, maxgen, flags);'
        $tableReplacement = '                    if (hTable)' + [Environment]::NewLine + '                    {' + [Environment]::NewLine + '                        guideXosNativeAotC011EC30HandleTableVisited(' + [Environment]::NewLine + '                            reinterpret_cast<uintptr_t>(hTable), i, static_cast<uint32_t>(uCPUindex));' + [Environment]::NewLine + '                        HndScanHandlesForGC(hTable, CheckPromoted, (uintptr_t)sc, 0, types, ARRAY_SIZE(types), condemned, maxgen, flags);' + [Environment]::NewLine + '                    }'
        $tableOffset = $objectHandleText.IndexOf($tableNeedle, $refStart, [System.StringComparison]::Ordinal)
        if ($tableOffset -lt 0) { throw "Locked Ref_CheckAlive handle-table branch was not found for C011EC30." }
        $objectHandleText = $objectHandleText.Substring(0, $tableOffset) + $tableReplacement + $objectHandleText.Substring($tableOffset + $tableNeedle.Length)
        $expectedCallbackNeedle = '                        HndScanHandlesForGC(hTable, CheckPromoted'
        $expectedCallbackOffset = $objectHandleText.IndexOf($expectedCallbackNeedle, $refStart, [System.StringComparison]::Ordinal)
        if ($expectedCallbackOffset -lt 0) { throw "Locked Ref_CheckAlive callback dispatch was not found for C011EC30." }
        $objectHandleText = $objectHandleText.Substring(0, $expectedCallbackOffset) + '                        guideXosNativeAotC011EC30HandleCallbackExpected(reinterpret_cast<uintptr_t>(CheckPromoted));' + [Environment]::NewLine + $objectHandleText.Substring($expectedCallbackOffset)
        $checkNeedle = @'
    if (!g_theGCHeap->IsPromoted(*ppRef))
    {
        LOG((LF_GC, LL_INFO100, LOG_HANDLE_OBJECT_CLASS("Severing Weak-", pObjRef, "to unreachable ", *pObjRef)));

        *ppRef = NULL;
    }
    else
    {
        LOG((LF_GC, LL_INFO1000000, "reachable " LOG_OBJECT_CLASS(*pObjRef)));
    }
'@
        $checkReplacement = @'
    const uintptr_t guideXosSlotAddress = reinterpret_cast<uintptr_t>(pObjRef);
    const uintptr_t guideXosTargetBefore = reinterpret_cast<uintptr_t>(*ppRef);
    guideXosNativeAotC011EC30LivenessCheckEntered(
        guideXosSlotAddress, guideXosTargetBefore);
    C011EC31_LIVENESS_HOOK
    if (!g_theGCHeap->IsPromoted(*ppRef))
    {
        guideXosNativeAotC011EC30LivenessDecisionObserved(0u);
        C011EC33_DEAD_DECISION_HOOK
        LOG((LF_GC, LL_INFO100, LOG_HANDLE_OBJECT_CLASS("Severing Weak-", pObjRef, "to unreachable ", *pObjRef)));

        C011EC32_CLEARING_STORE_HOOK
        *ppRef = NULL;
    }
    else
    {
        guideXosNativeAotC011EC30LivenessDecisionObserved(1u);
        C011EC33_LIVE_DECISION_HOOK
        LOG((LF_GC, LL_INFO1000000, "reachable " LOG_OBJECT_CLASS(*pObjRef)));
    }
    guideXosNativeAotC011EC30LivenessDecisionCompleted(
        guideXosSlotAddress, guideXosTargetBefore,
        reinterpret_cast<uintptr_t>(*ppRef));
'@
        $c31LivenessHook = if ($isC011EC31) {
            'guideXosNativeAotC011EC31LivenessCheckEntered(' + [Environment]::NewLine +
            '        guideXosSlotAddress, guideXosTargetBefore,' + [Environment]::NewLine +
            '        static_cast<uint32_t>(g_theGCHeap->WhichGeneration(*ppRef)),' + [Environment]::NewLine +
            '        guideXosTargetBefore,' + [Environment]::NewLine +
            '        guideXosTargetBefore != 0u' + [Environment]::NewLine +
            '            ? *reinterpret_cast<const uintptr_t *>(guideXosTargetBefore) : 0u,' + [Environment]::NewLine +
            '        static_cast<uintptr_t>(1u));'
        } elseif ($isC011EC32) {
            'guideXosNativeAotC011EC32LivenessCheckEntered(' + [Environment]::NewLine +
            '        guideXosSlotAddress, guideXosTargetBefore,' + [Environment]::NewLine +
            '        static_cast<uint32_t>(g_theGCHeap->WhichGeneration(*ppRef)),' + [Environment]::NewLine +
            '        guideXosTargetBefore,' + [Environment]::NewLine +
            '        guideXosTargetBefore != 0u' + [Environment]::NewLine +
            '            ? *reinterpret_cast<const uintptr_t *>(guideXosTargetBefore) : 0u,' + [Environment]::NewLine +
            '        static_cast<uintptr_t>(1u));'
        } else {
            ''
        }
        $c33LivenessHook = if ($isC011EC33) {
            'guideXosNativeAotC011EC33LivenessCheckEntered(' + [Environment]::NewLine +
            '        guideXosSlotAddress, guideXosTargetBefore,' + [Environment]::NewLine +
            '        static_cast<uintptr_t>(g_theGCHeap->WhichGeneration(*ppRef)),' + [Environment]::NewLine +
            '        guideXosTargetBefore,' + [Environment]::NewLine +
            '        guideXosTargetBefore != 0u' + [Environment]::NewLine +
            '            ? *reinterpret_cast<const uintptr_t *>(guideXosTargetBefore) : 0u,' + [Environment]::NewLine +
            '        static_cast<uintptr_t>(1u));'
        } else { '' }
        $clearHook = if ($isC011EC32) {
            'guideXosNativeAotC011EC32ClearingStoreEntered(' + [Environment]::NewLine +
            '            guideXosSlotAddress, guideXosTargetBefore);'
        } else { '' }
        $c33ClearHook = if ($isC011EC33) {
            'guideXosNativeAotC011EC33ClearingStoreEntered(' + [Environment]::NewLine +
            '            guideXosSlotAddress, guideXosTargetBefore);'
        } else { '' }
        $checkReplacement = $checkReplacement.Replace('    C011EC31_LIVENESS_HOOK', ('    ' + $c31LivenessHook).TrimEnd())
        $checkReplacement = $checkReplacement.Replace(
            '    guideXosNativeAotC011EC30LivenessCheckEntered(' + [Environment]::NewLine +
            '        guideXosSlotAddress, guideXosTargetBefore);',
            '    guideXosNativeAotC011EC30LivenessCheckEntered(' + [Environment]::NewLine +
            '        guideXosSlotAddress, guideXosTargetBefore);' +
            $(if ($isC011EC33) { [Environment]::NewLine + '    ' + $c33LivenessHook.TrimEnd() } else { '' }))
        if ($isC011EC33) {
            $c32LivenessNeedle = '    guideXosNativeAotC011EC32LivenessCheckEntered(' + [Environment]::NewLine +
                '        guideXosSlotAddress, guideXosTargetBefore,' + [Environment]::NewLine +
                '        static_cast<uint32_t>(g_theGCHeap->WhichGeneration(*ppRef)),' + [Environment]::NewLine +
                '        guideXosTargetBefore,' + [Environment]::NewLine +
                '        guideXosTargetBefore != 0u' + [Environment]::NewLine +
                '            ? *reinterpret_cast<const uintptr_t *>(guideXosTargetBefore) : 0u,' + [Environment]::NewLine +
                '        static_cast<uintptr_t>(1u));'
            if (-not $checkReplacement.Contains($c32LivenessNeedle)) { throw "C011EC33 could not find the C011EC32 liveness hook insertion point." }
            $checkReplacement = $checkReplacement.Replace(
                $c32LivenessNeedle,
                $c32LivenessNeedle + [Environment]::NewLine + '    ' + $c33LivenessHook.TrimEnd())
        }
        $checkReplacement = $checkReplacement.Replace('        C011EC32_CLEARING_STORE_HOOK', ('        ' + $clearHook).TrimEnd())
        $checkReplacement = $checkReplacement.Replace(
            '        ' + $clearHook.TrimEnd(),
            '        ' + $clearHook.TrimEnd() + $(if ($isC011EC33) { [Environment]::NewLine + '        ' + $c33ClearHook.TrimEnd() } else { '' }))
        $c33DeadDecisionHook = if ($isC011EC33) {
            'guideXosNativeAotC011EC33LivenessDecisionObserved(0u);'
        } else { '' }
        $c33LiveDecisionHook = if ($isC011EC33) {
            'guideXosNativeAotC011EC33LivenessDecisionObserved(1u);'
        } else { '' }
        $checkReplacement = $checkReplacement.Replace(
            '        C011EC33_DEAD_DECISION_HOOK',
            ('        ' + $c33DeadDecisionHook).TrimEnd())
        $checkReplacement = $checkReplacement.Replace(
            '        C011EC33_LIVE_DECISION_HOOK',
            ('        ' + $c33LiveDecisionHook).TrimEnd())
        $checkNeedle = $checkNeedle.Replace([string][char]10, [Environment]::NewLine)
        if (-not $objectHandleText.Contains($checkNeedle)) { throw "Locked CheckPromoted production decision was not found for C011EC30." }
        $objectHandleText = $objectHandleText.Replace($checkNeedle, $checkReplacement.TrimEnd())
        $variableNeedle = '#ifdef FEATURE_VARIABLE_HANDLES'
        $variableOffset = $objectHandleText.IndexOf($variableNeedle, $refStart, [System.StringComparison]::Ordinal)
        if ($variableOffset -lt 0) { throw "Locked Ref_CheckAlive variable-handle boundary was not found for C011EC30." }
        $refEndNeedle = '#endif' + [Environment]::NewLine + '}'
        $refEndOffset = $objectHandleText.IndexOf($refEndNeedle, $variableOffset, [System.StringComparison]::Ordinal)
        if ($refEndOffset -lt 0) { throw "Locked Ref_CheckAlive closing boundary was not found for C011EC30." }
        $refEndReplacement = '#endif' + [Environment]::NewLine + '    guideXosNativeAotC011EC30HandleScanCompleted(condemned, maxgen);' + [Environment]::NewLine + '}'
        $objectHandleText = $objectHandleText.Substring(0, $refEndOffset) + $refEndReplacement + $objectHandleText.Substring($refEndOffset + $refEndNeedle.Length)
        if ($objectHandleText -notmatch 'guideXosNativeAotC011EC30HandleMapRootRead' -or
            $objectHandleText -notmatch 'guideXosNativeAotC011EC30BucketVisited' -or
            $objectHandleText -notmatch 'guideXosNativeAotC011EC30HandleTableVisited' -or
            $objectHandleText -notmatch 'guideXosNativeAotC011EC30LivenessDecisionCompleted') {
            throw "C011EC30 objecthandle instrumentation did not match the map, table, and production liveness boundaries."
        }
        Set-Content -LiteralPath $objectHandleC30Source -Value $objectHandleText -Encoding ASCII

        $lockedHandleTableScanPath = Join-Path $lockedSourceRoot "src\coreclr\gc\handletablescan.cpp"
        Require-File $lockedHandleTableScanPath "Locked NativeAOT handletablescan source"
        $handleTableScanText = Get-Content -LiteralPath $lockedHandleTableScanPath -Raw
        $handleTableScanDeclaration = @(
            'extern "C" void __cdecl guideXosNativeAotC011EC30SegmentVisited(uintptr_t tableAddress, uintptr_t segmentAddress, uintptr_t nextSegmentAddress);',
            'extern "C" void __cdecl guideXosNativeAotC011EC30BlockVisited(uintptr_t segmentAddress, uint32_t blockIndex, uint32_t blockType, uint32_t generationWord, uint32_t ageMask, uintptr_t blockFirstSlotAddress);',
            'extern "C" void __cdecl guideXosNativeAotC011EC30HandleSlotInspected(uintptr_t slotAddress, uintptr_t value, uint32_t userData);',
            'extern "C" void __cdecl guideXosNativeAotC011EC30HandleSlotCandidate(uintptr_t slotAddress, uintptr_t value);',
            'extern "C" void __cdecl guideXosNativeAotC011EC30HandleCallbackDispatched(uintptr_t callbackAddress);',
            'extern "C" void __cdecl guideXosNativeAotC011EC30HandleSlotNull(uintptr_t slotAddress, uintptr_t value);'
        ) -join [Environment]::NewLine
        $handleTableScanText = $handleTableScanText.Replace(
            '#include "objecthandle.h"',
            '#include "objecthandle.h"' + [Environment]::NewLine + [Environment]::NewLine + $handleTableScanDeclaration)
        $segmentNeedle = '        if (uTypeCount >= 1)'
        $segmentReplacement = '        guideXosNativeAotC011EC30SegmentVisited(' + [Environment]::NewLine + '            reinterpret_cast<uintptr_t>(pTable),' + [Environment]::NewLine + '            reinterpret_cast<uintptr_t>(pSegment),' + [Environment]::NewLine + '            reinterpret_cast<uintptr_t>(pSegment->pNextSegment));' + [Environment]::NewLine + [Environment]::NewLine + $segmentNeedle
        $segmentOffset = $handleTableScanText.IndexOf($segmentNeedle, [System.StringComparison]::Ordinal)
        if ($segmentOffset -lt 0) { throw "Locked TableScanHandles segment boundary was not found for C011EC30." }
        $handleTableScanText = $handleTableScanText.Substring(0, $segmentOffset) + $segmentReplacement + $handleTableScanText.Substring($segmentOffset + $segmentNeedle.Length)
        $blockNeedle = '        uint32_t dwClumpMask = COMPUTE_CLUMP_MASK(*pdwGen, dwAgeMask);'
        $blockReplacement = '        const uint32_t guideXosBlockIndex = static_cast<uint32_t>(pdwGen - (uint32_t *)pSegment->rgGeneration);' + [Environment]::NewLine + '        guideXosNativeAotC011EC30BlockVisited(' + [Environment]::NewLine + '            reinterpret_cast<uintptr_t>(pSegment), guideXosBlockIndex,' + [Environment]::NewLine + '            static_cast<uint32_t>(static_cast<int>(pSegment->rgBlockType[guideXosBlockIndex])),' + [Environment]::NewLine + '            *pdwGen, dwAgeMask,' + [Environment]::NewLine + '            reinterpret_cast<uintptr_t>(pSegment->rgValue + (guideXosBlockIndex * HANDLE_HANDLES_PER_BLOCK)));' + [Environment]::NewLine + [Environment]::NewLine + $blockNeedle
        $blockOffset = $handleTableScanText.IndexOf($blockNeedle, [System.StringComparison]::Ordinal)
        if ($blockOffset -lt 0) { throw "Locked BlockScanBlocksEphemeral block boundary was not found for C011EC30." }
        $handleTableScanText = $handleTableScanText.Substring(0, $blockOffset) + $blockReplacement + $handleTableScanText.Substring($blockOffset + $blockNeedle.Length)
        $slotNeedle = '        if (!HndIsNullOrDestroyedHandle(*pValue))' + [Environment]::NewLine + '        {'
        $slotReplacement = '        guideXosNativeAotC011EC30HandleSlotInspected(' + [Environment]::NewLine + '            reinterpret_cast<uintptr_t>(pValue), reinterpret_cast<uintptr_t>(*pValue),' + [Environment]::NewLine + '            0u);' + [Environment]::NewLine + '        if (!HndIsNullOrDestroyedHandle(*pValue))' + [Environment]::NewLine + '        {' + [Environment]::NewLine + '            guideXosNativeAotC011EC30HandleSlotCandidate(' + [Environment]::NewLine + '                reinterpret_cast<uintptr_t>(pValue), reinterpret_cast<uintptr_t>(*pValue));'
        if (-not $handleTableScanText.Contains($slotNeedle)) { throw "Locked handle-slot scan boundary was not found for C011EC30." }
        $handleTableScanText = $handleTableScanText.Replace($slotNeedle, $slotReplacement)
        $callbackNeedle = '            // process this handle'
        $callbackReplacement = '            guideXosNativeAotC011EC30HandleCallbackDispatched(' + [Environment]::NewLine + '                reinterpret_cast<uintptr_t>(pfnScan));' + [Environment]::NewLine + [Environment]::NewLine + $callbackNeedle
        if (([regex]::Matches($handleTableScanText, [regex]::Escape($callbackNeedle))).Count -ne 2) { throw "Locked handle callback dispatch boundary was not found twice for C011EC30." }
        $handleTableScanText = $handleTableScanText.Replace($callbackNeedle, $callbackReplacement)
        $slotCloseNeedle = '            pfnScan(pValue, NULL, param1, param2);' + [Environment]::NewLine + '        }' + [Environment]::NewLine + [Environment]::NewLine + '        // on to the next handle'
        $slotCloseReplacement = '            pfnScan(pValue, NULL, param1, param2);' + [Environment]::NewLine + '        }' + [Environment]::NewLine + '        else' + [Environment]::NewLine + '        {' + [Environment]::NewLine + '            guideXosNativeAotC011EC30HandleSlotNull(' + [Environment]::NewLine + '                reinterpret_cast<uintptr_t>(pValue), reinterpret_cast<uintptr_t>(*pValue));' + [Environment]::NewLine + '        }' + [Environment]::NewLine + [Environment]::NewLine + '        // on to the next handle'
        if (-not $handleTableScanText.Contains($slotCloseNeedle)) { throw "Locked no-user-data handle-slot close boundary was not found for C011EC30." }
        $handleTableScanText = $handleTableScanText.Replace($slotCloseNeedle, $slotCloseReplacement)
        if ($handleTableScanText -notmatch 'guideXosNativeAotC011EC30SegmentVisited' -or
            $handleTableScanText -notmatch 'guideXosNativeAotC011EC30BlockVisited' -or
            $handleTableScanText -notmatch 'guideXosNativeAotC011EC30HandleSlotCandidate') {
            throw "C011EC30 handletablescan instrumentation did not match the authentic segment/block/slot path."
        }
        Set-Content -LiteralPath $handleTableScanSource -Value $handleTableScanText -Encoding ASCII
        if ($isC011EC31 -or $isC011EC32) {
            $lockedHandleTableHelpersPath = Join-Path $lockedSourceRoot "src\coreclr\nativeaot\Runtime\HandleTableHelpers.cpp"
            Require-File $lockedHandleTableHelpersPath "Locked NativeAOT HandleTableHelpers source"
            $handleTableHelpersText = Get-Content -LiteralPath $lockedHandleTableHelpersPath -Raw
            $handlePrefix = if ($isC011EC33) { 'C011EC33' } elseif ($isC011EC32) { 'C011EC32' } else { 'C011EC31' }
            $handleTableHelpersDeclaration = 'extern "C" void __cdecl guideXosNativeAot' + $handlePrefix + 'HandleAllocationEntered(uintptr_t allocationEntryAddress);' + [Environment]::NewLine + 'extern "C" void __cdecl guideXosNativeAot' + $handlePrefix + 'HandleAllocated(uintptr_t handleSlot, uintptr_t target, uint32_t handleType, uintptr_t allocationContinuationAddress);'
            $handleTableHelpersText = $handleTableHelpersText.Replace(
                '#include "gchandleutilities.h"',
                '#include "gchandleutilities.h"' + [Environment]::NewLine + $handleTableHelpersDeclaration)
            $handleAllocNeedle = '    return GCHandleUtilities::GetGCHandleManager()->GetGlobalHandleStore()->CreateHandleOfType(pObject, (HandleType)type);'
            $handleAllocReplacement = '    guideXosNativeAot' + $handlePrefix + 'HandleAllocationEntered(' + [Environment]::NewLine + '        reinterpret_cast<uintptr_t>(_ReturnAddress()));' + [Environment]::NewLine + '    OBJECTHANDLE guideXosHandle = GCHandleUtilities::GetGCHandleManager()->GetGlobalHandleStore()->CreateHandleOfType(pObject, (HandleType)type);' + [Environment]::NewLine + '    guideXosNativeAot' + $handlePrefix + 'HandleAllocated(' + [Environment]::NewLine + '        reinterpret_cast<uintptr_t>(guideXosHandle),' + [Environment]::NewLine + '        reinterpret_cast<uintptr_t>(pObject),' + [Environment]::NewLine + '        static_cast<uint32_t>(type),' + [Environment]::NewLine + '        reinterpret_cast<uintptr_t>(_ReturnAddress()));' + [Environment]::NewLine + '    return guideXosHandle;'
            if (-not $handleTableHelpersText.Contains($handleAllocNeedle)) { throw "Locked RhpHandleAlloc production allocation boundary was not found for $handlePrefix." }
            $handleTableHelpersText = $handleTableHelpersText.Replace($handleAllocNeedle, $handleAllocReplacement)
            if ($handleTableHelpersText -notmatch ('guideXosNativeAot' + $handlePrefix + 'HandleAllocated')) { throw "$handlePrefix production RhpHandleAlloc hook was not inserted." }
            $handleTableHelpersTarget = if ($isC011EC33) { $handleTableHelpersC33Source } elseif ($isC011EC32) { $handleTableHelpersC32Source } else { $handleTableHelpersC31Source }
            Set-Content -LiteralPath $handleTableHelpersTarget -Value $handleTableHelpersText -Encoding ASCII
        }
        $objectHandleText = Get-Content -LiteralPath $lockedObjectHandlePath -Raw
        $objectHandleDeclaration = 'extern "C" void __cdecl guideXosNativeAotC011EC29HandleScanEntered(uint32_t condemned, uint32_t maxGeneration, uintptr_t scanContext);' + [Environment]::NewLine + 'extern "C" __declspec(noreturn) void __cdecl guideXosNativeAotC011EC29HandleMapRead(uintptr_t mapAddress, uintptr_t bucketsFieldAddress, uintptr_t bucketsValue, uintptr_t maxIndex, uint32_t flags);'
        $objectHandleText = $objectHandleText.Replace(
            '#include "objecthandle.h"',
            '#include "objecthandle.h"' + [Environment]::NewLine + [Environment]::NewLine + $objectHandleDeclaration)
        $refPattern = '(?m)^void Ref_CheckAlive\(uint32_t condemned, uint32_t maxgen, ScanContext \*sc\)\r?\n\{'
        $refReplacement = 'void Ref_CheckAlive(uint32_t condemned, uint32_t maxgen, ScanContext *sc)' + [Environment]::NewLine + '{' + [Environment]::NewLine + '    guideXosNativeAotC011EC29HandleScanEntered(condemned, maxgen, reinterpret_cast<uintptr_t>(sc));'
        $objectHandleText = [regex]::Replace($objectHandleText, $refPattern, $refReplacement, 1)
        $refStart = $objectHandleText.IndexOf('void Ref_CheckAlive(', [System.StringComparison]::Ordinal)
        if ($refStart -lt 0) { throw "Locked Ref_CheckAlive definition was not found for C011EC29." }
        $mapNeedle = '    HandleTableMap *walk = &g_HandleTableMap;'
        $mapOffset = $objectHandleText.IndexOf($mapNeedle, $refStart, [System.StringComparison]::Ordinal)
        if ($mapOffset -lt 0) { throw "Locked Ref_CheckAlive HandleTableMap walk was not found for C011EC29." }
        $mapReplacement = '    HandleTableMap *walk = &g_HandleTableMap;' + [Environment]::NewLine + '    guideXosNativeAotC011EC29HandleMapRead(' + [Environment]::NewLine + '        reinterpret_cast<uintptr_t>(&g_HandleTableMap),' + [Environment]::NewLine + '        reinterpret_cast<uintptr_t>(&walk->pBuckets),' + [Environment]::NewLine + '        reinterpret_cast<uintptr_t>(walk->pBuckets),' + [Environment]::NewLine + '        static_cast<uintptr_t>(walk->dwMaxIndex),' + [Environment]::NewLine + '        flags);'
        $objectHandleText = $objectHandleText.Substring(0, $mapOffset) + $mapReplacement + $objectHandleText.Substring($mapOffset + $mapNeedle.Length)
        if ($objectHandleText -notmatch 'guideXosNativeAotC011EC29HandleScanEntered' -or
            $objectHandleText -notmatch 'guideXosNativeAotC011EC29HandleMapRead' -or
            $objectHandleText -notmatch 'reinterpret_cast<uintptr_t>\(walk->pBuckets\)') {
            throw "C011EC29 objecthandle instrumentation did not match Ref_CheckAlive and its first map read."
        }
        Set-Content -LiteralPath $objectHandleSource -Value $objectHandleText -Encoding ASCII
    }
    $baselineDescription = if ($isC011EC33) {
        "experiment=single-managed-mutator Workstation GC one genuine short-weak handle across two authentic collections; Collection 1 uses a no-inline managed helper frame as the final strong root and Collection 2 begins after that frame returns"
    } elseif ($isNextGenuineRootProvider) {
        "experiment=single-managed-mutator Workstation GC first genuine root callback returned and root enumeration stopped at the next genuine non-null provider candidate"
    } elseif ($isFirstRootPostQueueMarkDecision) {
        "experiment=single-managed-mutator Workstation GC genuine thread-static storage root completed the first queue_mark post-queue old_o/null/mark-state decision"
    } elseif ($isFirstRootFirstMarkMutation) {
        "experiment=single-managed-mutator Workstation GC genuine thread-static storage root entered mark_object_simple and stopped after the smallest safe queue_mark mutation unit"
    } elseif ($isFirstRootPreMarkBoundary) {
        "experiment=single-managed-mutator Workstation GC real thread-static storage root and source-required condemned=true pre-mark path stopped before mark_object_simple"
    } elseif ($isFirstRootCondemnedGenerationDecision) {
        "experiment=single-managed-mutator Workstation GC real thread-static storage root and exactly one source-valid is_in_condemned_gc(o) decision"
    } elseif ($isFirstRootHeapResolution) {
        "experiment=single-managed-mutator Workstation GC real thread-static storage root and exactly one source-valid HEAP_FROM_THREAD/heap_of(o) heap-resolution transition"
    } elseif ($isFirstRootMembershipClassification) {
        "experiment=single-managed-mutator Workstation GC real thread-static storage root and exactly one managed-range membership classification"
    } elseif ($isFirstRootCallbackEntry) {
        "experiment=single-managed-mutator Workstation GC real thread-static storage root and first GCHeap::Promote callback entry"
    } elseif ($isFirstNonNullRoot) {
        "experiment=single-managed-mutator Workstation GC real thread-static proof root and first non-null candidate callback boundary"
    } elseif ($isFirstPerThreadRootProvider) {
        "experiment=single-managed-mutator Workstation GC real FOREACH_THREAD enumeration and first per-thread root provider entry"
    } elseif ($isAllocationContextFixupRootBoundary) {
        "experiment=single-managed-mutator Workstation GC fix_allocation_contexts(TRUE) completion and first root-dispatch boundary"
    } else {
        "experiment=single-managed-mutator Workstation GC SuspendEE completion and post-DisablePreemptiveGC boundary"
    }
    $safeStopDescription = if ($isC011EC33) {
        "safeStop=after Collection 2 production CheckPromoted reports the same weak slot dead and the real short-weak store clears it; if Collection 1 cannot reach EE restart, classify the first post-weak completion phase as Outcome D"
    } elseif ($isNextGenuineRootProvider) {
        "safeStop=after the first root callback returned and the next genuine non-null Object** candidate/provider was captured, before its Promote callback or any second queue mutation"
    } elseif ($isFirstRootPostQueueMarkDecision) {
        "safeStop=after the first queue_mark old_o == nullptr decision and before marked(old_o), or after the read-only marked decision, before any subsequent mutation"
    } elseif ($isFirstRootFirstMarkMutation) {
        "safeStop=after queue_mark slot_table[slot_index]=o and curr_slot_index advancement, before old_o mark-state read"
    } elseif ($isFirstRootPreMarkBoundary) {
        "safeStop=after all source-required non-mutating GCHeap::Promote true-branch logic and immediately before hpt->mark_object_simple"
    } elseif ($isFirstRootCondemnedGenerationDecision) {
        "safeStop=after exactly one real gc_heap::is_in_condemned_gc(o) decision and before the GCHeap::Promote true/false continuation"
    } elseif ($isFirstRootHeapResolution) {
        "safeStop=after exactly one real gc_heap::heap_of(o) result and immediately before gc_heap::is_in_condemned_gc(o) at locked gc.cpp:49499"
    } elseif ($isFirstRootMembershipClassification) {
        "safeStop=after exactly one real gc_heap::is_in_find_object_range boolean result returned to GCHeap::Promote and immediately before HEAP_FROM_THREAD at locked gc.cpp:49494"
    } elseif ($isFirstRootCallbackEntry) {
        "safeStop=after the real GCHeap::Promote callback entered with live ABI arguments and after its required *ppObject load, immediately before gc_heap::is_in_find_object_range"
    } elseif ($isFirstPerThreadRootProvider) {
        if ($isFirstNonNullRoot) {
            "safeStop=after a genuine non-null managed thread-static candidate load and before the first root callback"
        } elseif ($isFirstRootCandidateLoad) {
            "safeStop=after the real EnumGcRef path's one explicit slot load and before callback/semantic processing"
        } else {
            "safeStop=after real ThreadStore::Iterator enumeration and Thread::GetThreadStaticStorage entry before EnumGcRef candidate access"
        }
    } elseif ($isAllocationContextFixupRootBoundary) {
        "safeStop=after real allocation-context enumeration and GcStartWork; at GcScanRoots entry before FOREACH_THREAD"
    } else {
        "safeStop=after real ThreadStore::LockThreadStore and SuspendAllThreads return, at GcStartWork entry"
    }
    Set-Content -LiteralPath (Join-Path $runRoot "baseline.txt") -Value @(
        $baselineDescription,
        "arrayLength=4096",
        "hardAllocationLimit=256",
        "heapReserveCommit=locked adapted Workstation GC configuration",
        "nativeAotSourceCommit=$lockedCommit",
        "runtimePack=9.0.0 AMD64 Workstation GC interface=5.3 EE=2",
        "collectionPath=soh_try_fit->a_fit_segment_end_p->a_state_trigger_ephemeral_gc->trigger_ephemeral_gc->GCHeap::GarbageCollectGeneration->GCToEEInterface::SuspendEE->GCHeap::GarbageCollect before fix_allocation_contexts",
        $safeStopDescription,
        "activePalArchiveSha256=$(Hash-File $activeArchive)",
        "normalKernelSha256=$normalKernelHash"
    ) -Encoding ASCII

    $runtimeBat = Write-Batch "build-single-thread-suspend-ee-runtime-pack.bat" @"
@echo off
setlocal
call "$vsBat" >nul
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /GS- /GR- /EHs-c- /Zl /Oi /O2 /Brepro /DWIN32 /D_WIN32 /D_WIN64 /DHOST_AMD64 /DTARGET_AMD64 /DHOST_64BIT /DTARGET_64BIT /DHOST_WINDOWS /DTARGET_WINDOWS /DNATIVEAOT /DFEATURE_NATIVEAOT /DGUIDEXOS_NATIVEAOT_MANAGED_ALLOCATION /DGUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION /DGUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_ARRAY_LENGTH=4096 /DGUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_HARD_LIMIT=256 /I"$nativeAotRoot\Runtime" /I"$nativeAotRoot\Runtime\inc" /I"$nativeAotRoot\Runtime\windows" /I"$sourceRoot" /I"$palSourceRoot" /I"$sourceRoot\native" /I"$sourceRoot\gc" /I"$sourceRoot\gc\env" /I"$sourceRoot\pal\src\include" /Fo:"$platformObj" "$platformSource"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro /I"$(Join-Path $root 'tools\dotnet\runtime-pack\src\platform')" /Fo:"$nativeUnwindPrimitiveObj" "$nativeUnwindPrimitiveSource"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro /DGXOS_BARE_METAL /DGUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_ALLOCATION /I"$(Join-Path $root 'tools\dotnet\runtime-pack\src\platform')" /I"$nativeAotRoot\Runtime" /I"$sourceRoot" /I"$sourceRoot\native" /I"$sourceRoot\gc" /I"$sourceRoot\gc\env" /I"$sourceRoot\pal\src\include" /Fo:"$gcBridgeBoundary" "$gcBridgeSource"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /GS- /GR- /EHs-c- /Zl /Oi /O2 /Brepro /DGXOS_BARE_METAL /DGXOS_TRUE_VIRTUAL_MEMORY /D_FEATURE_NATIVEAOT /DNATIVEAOT /DTARGET_AMD64 /DHOST_AMD64 /DHOST_64BIT /D_WIN64 /DGUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_ALLOCATION /I"$sourceRoot\gc" /I"$sourceRoot\gc\env" /I"$nativeAotRoot\Runtime" /I"$sourceRoot\native" /Fo:"$(Join-Path $runtimeRoot 'guidexos_gcenv.single-thread-suspend-ee.obj')" "$(Join-Path $root 'tools\dotnet\runtime-pack\src\gcenv\guidexos_gcenv.cpp')"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro /DWIN32 /D_WIN32 /D_WIN64 /DHOST_AMD64 /DTARGET_AMD64 /DHOST_64BIT /DTARGET_64BIT /DHOST_WINDOWS /DTARGET_WINDOWS /DNATIVEAOT /DFEATURE_NATIVEAOT /DFEATURE_HIJACK /DFEATURE_SUSPEND_REDIRECTION /DFEATURE_PERFTRACING /DFEATURE_BASICFREEZE /DFEATURE_CONSERVATIVE_GC /DFEATURE_CUSTOM_IMPORTS /DFEATURE_DYNAMIC_CODE /DFEATURE_CACHED_INTERFACE_DISPATCH /DVERIFY_HEAP /D_LIB /DLPVOID=void* /DGUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_ALLOCATION /I"$nativeAotRoot\Runtime" /I"$nativeAotRoot\Runtime\windows" /I"$sourceRoot" /I"$palSourceRoot" /I"$sourceRoot\native" /I"$sourceRoot\gc" /I"$sourceRoot\gc\env" /I"$nativeAotRoot\Runtime\inc" /I"$nativeAotRoot\Runtime\eventpipe" /I"$(Join-Path $root 'tools\dotnet\runtime-pack\src\platform')" /I"$sourceRoot\pal\src\include" /FI"$sourceRoot\gc\env\common.h" /Fo:"$gcEnvEe" "$gcEnvEeSource"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro /DWIN32 /D_WIN32 /D_WIN64 /DHOST_AMD64 /DTARGET_AMD64 /DHOST_64BIT /DTARGET_64BIT /DHOST_WINDOWS /DNATIVEAOT /DFEATURE_NATIVEAOT /DFEATURE_HIJACK /DFEATURE_SUSPEND_REDIRECTION /DFEATURE_PERFTRACING /DFEATURE_BASICFREEZE /DFEATURE_CONSERVATIVE_GC /DFEATURE_CUSTOM_IMPORTS /DFEATURE_DYNAMIC_CODE /DFEATURE_CACHED_INTERFACE_DISPATCH /DVERIFY_HEAP /D_LIB /DGUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_ALLOCATION /I"$nativeAotRoot\Runtime" /I"$nativeAotRoot\Runtime\windows" /I"$sourceRoot" /I"$palSourceRoot" /I"$sourceRoot\native" /I"$sourceRoot\gc" /I"$sourceRoot\gc\env" /I"$nativeAotRoot\Runtime\inc" /I"$nativeAotRoot\Runtime\eventpipe" /I"$(Join-Path $root 'tools\dotnet\runtime-pack\src\platform')" /I"$sourceRoot\pal\src\include" /FI"$sourceRoot\gc\env\common.h" /Fo:"$probeObj" "$probeSource"
if errorlevel 1 exit /b %errorlevel%
exit /b 0
"@
    if ($isCandidateLoadEnumeration) {
        $runtimeBatText = Get-Content -LiteralPath $runtimeBat -Raw
        $gcEnumCompileLine = 'cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro /DWIN32 /D_WIN32 /D_WIN64 /DHOST_AMD64 /DTARGET_AMD64 /DTARGET_64BIT /DHOST_64BIT /DHOST_WINDOWS /DNATIVEAOT /DFEATURE_NATIVEAOT /DFEATURE_HIJACK /DFEATURE_SUSPEND_REDIRECTION /DFEATURE_PERFTRACING /DFEATURE_BASICFREEZE /DFEATURE_CONSERVATIVE_GC /DFEATURE_CUSTOM_IMPORTS /DFEATURE_DYNAMIC_CODE /DFEATURE_CACHED_INTERFACE_DISPATCH /DVERIFY_HEAP /D_LIB /DLPVOID=void* /I"{0}\Runtime" /I"{0}\Runtime\windows" /I"{1}" /I"{1}\native" /I"{1}\gc" /I"{1}\gc\env" /I"{0}\Runtime\inc" /I"{0}\Runtime\eventpipe" /I"{2}" /I"{5}" /I"{1}\pal\src\include" /FI"{1}\gc\env\common.h" /Fo:"{3}" "{4}"' -f $nativeAotRoot, $sourceRoot, (Join-Path $root 'tools\dotnet\runtime-pack\src\platform'), $gcEnum, $gcEnumSource, $palSourceRoot
        $runtimeBatText = $runtimeBatText.Replace("exit /b 0", "$gcEnumCompileLine`r`nif errorlevel 1 exit /b %errorlevel%`r`nexit /b 0")
        Set-Content -LiteralPath $runtimeBat -Value $runtimeBatText -Encoding ASCII
    }
    if ($isC011EC30) {
        $runtimeBatText = Get-Content -LiteralPath $runtimeBat -Raw
        $objectHandleC30CompileLine = $gcEnumCompileLine.Replace($gcEnum, $objectHandleC30Obj).Replace($gcEnumSource, $objectHandleC30Source)
        $objectHandleC31Define = if ($isC011EC31) { ' /DGUIDEXOS_NATIVEAOT_C011EC31_LIVE_SHORT_WEAK' } else { '' }
        $objectHandleC32Define = if ($isC011EC32) { ' /DGUIDEXOS_NATIVEAOT_C011EC32_DEAD_SHORT_WEAK' } else { '' }
        $objectHandleC33Define = if ($isC011EC33) { ' /DGUIDEXOS_NATIVEAOT_C011EC33_LIFETIME_TRANSITION' } else { '' }
        $objectHandleC30CompileLine = $objectHandleC30CompileLine.Replace('/DLPVOID=void* ', "/DFEATURE_EVENT_TRACE /DSKIP_TRACING_DEFINITIONS /DGUIDEXOS_NATIVEAOT_C011EC29_POST_MARK_PHASE /DGUIDEXOS_NATIVEAOT_C011EC30_HANDLE_SCAN$objectHandleC31Define$objectHandleC32Define$objectHandleC33Define ")
        $runtimeBatText = $runtimeBatText.Replace('exit /b 0', $objectHandleC30CompileLine + [Environment]::NewLine + 'if errorlevel 1 exit /b %errorlevel%' + [Environment]::NewLine + 'exit /b 0')
        $handleTableScanCompileLine = $gcEnumCompileLine.Replace($gcEnum, $handleTableScanObj).Replace($gcEnumSource, $handleTableScanSource)
        $handleTableScanCompileLine = $handleTableScanCompileLine.Replace('/DLPVOID=void* ', '/DFEATURE_EVENT_TRACE /DSKIP_TRACING_DEFINITIONS /DGUIDEXOS_NATIVEAOT_C011EC30_HANDLE_SCAN ')
        $runtimeBatText = $runtimeBatText.Replace('exit /b 0', $handleTableScanCompileLine + [Environment]::NewLine + 'if errorlevel 1 exit /b %errorlevel%' + [Environment]::NewLine + 'exit /b 0')
        if ($isC011EC31 -or $isC011EC32) {
            $handleTableHelpersCompileLine = $gcEnumCompileLine.Replace($gcEnum, $handleTableHelpersC31Obj).Replace($gcEnumSource, $handleTableHelpersC31Source)
            if ($isC011EC33) {
                $handleTableHelpersCompileLine = $handleTableHelpersCompileLine.Replace($handleTableHelpersC31Obj, $handleTableHelpersC33Obj).Replace($handleTableHelpersC31Source, $handleTableHelpersC33Source)
            } elseif ($isC011EC32) {
                $handleTableHelpersCompileLine = $handleTableHelpersCompileLine.Replace($handleTableHelpersC31Obj, $handleTableHelpersC32Obj).Replace($handleTableHelpersC31Source, $handleTableHelpersC32Source)
            }
            $handleMacro = if ($isC011EC33) { 'GUIDEXOS_NATIVEAOT_C011EC33_LIFETIME_TRANSITION' } elseif ($isC011EC32) { 'GUIDEXOS_NATIVEAOT_C011EC32_DEAD_SHORT_WEAK' } else { 'GUIDEXOS_NATIVEAOT_C011EC31_LIVE_SHORT_WEAK' }
            $handleTableHelpersCompileLine = $handleTableHelpersCompileLine.Replace('/DLPVOID=void* ', "/DFEATURE_EVENT_TRACE /DSKIP_TRACING_DEFINITIONS /D$handleMacro ")
            $runtimeBatText = $runtimeBatText.Replace('exit /b 0', $handleTableHelpersCompileLine + [Environment]::NewLine + 'if errorlevel 1 exit /b %errorlevel%' + [Environment]::NewLine + 'exit /b 0')
        }
        Set-Content -LiteralPath $runtimeBat -Value $runtimeBatText -Encoding ASCII
    }
    if ($isC011EC29 -and -not $isC011EC30) {
        $runtimeBatText = Get-Content -LiteralPath $runtimeBat -Raw
        $objectHandleCompileLine = $gcEnumCompileLine.Replace("$gcEnum`"", "$objectHandleObj`"").Replace("$gcEnumSource`"", "$objectHandleSource`"")
        $objectHandleCompileLine = $objectHandleCompileLine.Replace('/DLPVOID=void* ', '/DFEATURE_EVENT_TRACE /DSKIP_TRACING_DEFINITIONS /DGUIDEXOS_NATIVEAOT_C011EC29_POST_MARK_PHASE ')
        $runtimeBatText = $runtimeBatText.Replace("exit /b 0", "$objectHandleCompileLine`r`nif errorlevel 1 exit /b %errorlevel%`r`nexit /b 0")
        Set-Content -LiteralPath $runtimeBat -Value $runtimeBatText -Encoding ASCII
    }
    if ($isFirstRootCallbackEntry) {
        $runtimeBatText = Get-Content -LiteralPath $runtimeBat -Raw
        $gcWksCompileLine = 'cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro /DWIN32 /D_WIN32 /D_WIN64 /DHOST_AMD64 /DTARGET_AMD64 /DTARGET_64BIT /DHOST_64BIT /DHOST_WINDOWS /DNATIVEAOT /DFEATURE_NATIVEAOT /DFEATURE_HIJACK /DFEATURE_SUSPEND_REDIRECTION /DFEATURE_PERFTRACING /DFEATURE_BASICFREEZE /DFEATURE_CONSERVATIVE_GC /DFEATURE_CUSTOM_IMPORTS /DFEATURE_DYNAMIC_CODE /DFEATURE_CACHED_INTERFACE_DISPATCH /DVERIFY_HEAP /D_LIB /DLPVOID=void* /DGUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_ALLOCATION /DGUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_PER_THREAD_ROOT_PROVIDER_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CALLBACK_ENTRY_ALLOCATION /I"{0}" /I"{1}" /I"{1}\Runtime" /I"{1}\Runtime\windows" /I"{2}" /I"{2}\native" /I"{2}\gc" /I"{2}\gc\env" /I"{1}\Runtime\inc" /I"{1}\Runtime\eventpipe" /I"{3}" /I"{4}" /I"{2}\pal\src\include" /FI"{2}\gc\env\common.h" /Fo:"{5}" "{6}"' -f $nativeAotRoot, $sourceRoot, $sourceRoot, (Join-Path $root 'tools\dotnet\runtime-pack\src\platform'), $palSourceRoot, $gcWks, $gcWksSource
        if ($isFirstRootMembershipClassification) {
            $gcWksCompileLine = $gcWksCompileLine.Replace(
                '/DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CALLBACK_ENTRY_ALLOCATION ',
                '/DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CALLBACK_ENTRY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION_ALLOCATION ')
        }
        $runtimeBatText = $runtimeBatText.Replace("exit /b 0", "$gcWksCompileLine`r`nif errorlevel 1 exit /b %errorlevel%`r`nexit /b 0")
        Set-Content -LiteralPath $runtimeBat -Value $runtimeBatText -Encoding ASCII
    }
    if ($isAllocationContextFixupRootBoundary) {
        $runtimeBatText = Get-Content -LiteralPath $runtimeBat -Raw
        $runtimeBatText = $runtimeBatText.Replace(
            "/DGUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_ALLOCATION ",
            "/DGUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_ALLOCATION $proofDefine ")
        Set-Content -LiteralPath $runtimeBat -Value $runtimeBatText -Encoding ASCII
    }
    $c26ThreadCompileLine = if ($isC011EC26) {
            ('cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro /DWIN32 /D_WIN32 /D_WIN64 /DHOST_AMD64 /DTARGET_AMD64 /DTARGET_64BIT /DHOST_64BIT /DHOST_WINDOWS /DTARGET_WINDOWS /DNATIVEAOT /DFEATURE_NATIVEAOT /DFEATURE_HIJACK /DFEATURE_SUSPEND_REDIRECTION /DFEATURE_PERFTRACING /DFEATURE_BASICFREEZE /DFEATURE_CONSERVATIVE_GC /DLPVOID=void* /DGUIDEXOS_NATIVEAOT_C011EC26_STACK_COMPLETION /DGUIDEXOS_NATIVEAOT_C011EC26_PLATFORM_RHP_REVERSE_PINVOKE /I"{0}\Runtime" /I"{0}\Runtime\windows" /I"{1}" /I"{1}\native" /I"{1}\gc" /I"{1}\gc\env" /I"{0}\Runtime\inc" /I"{0}\Runtime\eventpipe" /I"{2}" /I"{3}" /FI"{1}\gc\env\common.h" /Fo:"{4}" "{5}"' -f $nativeAotRoot, $sourceRoot, (Join-Path $root 'tools\dotnet\runtime-pack\src\platform'), $palSourceRoot, $threadC011EC26Obj, $threadC011EC26Source) + [Environment]::NewLine + 'if errorlevel 1 exit /b %errorlevel%'
    } else {
        ''
    }
    $c011ec18CompileBat = $null
    $c20CompileDefine = if ($isC011EC20) { " /DGUIDEXOS_NATIVEAOT_C011EC20_UNWIND" } else { "" }
    if ($isTransitionFrameControlPc -or $isC011EC19) {
        $c011ec18CompileBat = Write-Batch "build-single-thread-suspend-ee-c011ec18-instrumentation.bat" @"
@echo off
setlocal
call "$vsBat" >nul
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro /DWIN32 /D_WIN32 /D_WIN64 /DHOST_AMD64 /DTARGET_AMD64 /DTARGET_64BIT /DHOST_64BIT /DHOST_WINDOWS /DTARGET_WINDOWS /DNATIVEAOT /DFEATURE_NATIVEAOT /DFEATURE_HIJACK /DFEATURE_SUSPEND_REDIRECTION /DFEATURE_PERFTRACING /DFEATURE_BASICFREEZE /DFEATURE_CONSERVATIVE_GC /DFEATURE_CUSTOM_IMPORTS /DFEATURE_DYNAMIC_CODE /DFEATURE_CACHED_INTERFACE_DISPATCH /DVERIFY_HEAP /D_LIB /DLPVOID=void* /DGUIDEXOS_NATIVEAOT_GC_STARTUP /I"$(Join-Path $root 'tools\dotnet\runtime-pack\src\platform')" /I"$nativeAotRoot\Runtime" /I"$nativeAotRoot\Runtime\windows" /I"$nativeAotRoot\Runtime\inc" /I"$sourceRoot" /I"$sourceRoot\native" /I"$sourceRoot\gc" /I"$sourceRoot\gc\env" /I"$palSourceRoot" /Fo:"$gcHelpersC011EC18Obj" "$gcHelpersC011EC18Source"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro /DWIN32 /D_WIN32 /D_WIN64 /DHOST_AMD64 /DTARGET_AMD64 /DTARGET_64BIT /DHOST_64BIT /DHOST_WINDOWS /DTARGET_WINDOWS /DNATIVEAOT /DFEATURE_NATIVEAOT /DFEATURE_HIJACK /DFEATURE_SUSPEND_REDIRECTION /DFEATURE_PERFTRACING /DFEATURE_BASICFREEZE /DFEATURE_CONSERVATIVE_GC /DFEATURE_CUSTOM_IMPORTS /DFEATURE_DYNAMIC_CODE /DFEATURE_CACHED_INTERFACE_DISPATCH /DVERIFY_HEAP /D_LIB /DLPVOID=void* /DGUIDEXOS_NATIVEAOT_C011EC18_NATIVE_RHP_NEW_ARRAY$(if ($isC011EC26) { ' /DGUIDEXOS_NATIVEAOT_C011EC26_STACK_COMPLETION' } else { '' }) /I"$nativeAotRoot\Runtime" /I"$nativeAotRoot\Runtime\windows" /I"$sourceRoot" /I"$sourceRoot\native" /I"$sourceRoot\gc" /I"$sourceRoot\gc\env" /I"$nativeAotRoot\Runtime\inc" /I"$nativeAotRoot\Runtime\eventpipe" /I"$(Join-Path $root 'tools\dotnet\runtime-pack\src\platform')" /I"$palSourceRoot" /FI"$sourceRoot\gc\env\common.h" /Fo:"$stackFrameIteratorObj" "$stackFrameIteratorSource"
if errorlevel 1 exit /b %errorlevel%
$(if ($isC011EC19) { "cl.exe /nologo /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro /DWIN32 /D_WIN32 /D_WIN64 /DHOST_AMD64 /DTARGET_AMD64 /DTARGET_64BIT /DHOST_64BIT /DHOST_WINDOWS /DTARGET_WINDOWS /DNATIVEAOT /DFEATURE_NATIVEAOT /DFEATURE_HIJACK /DFEATURE_SUSPEND_REDIRECTION /DFEATURE_PERFTRACING /DFEATURE_BASICFREEZE /DFEATURE_CONSERVATIVE_GC /DFEATURE_CUSTOM_IMPORTS /DFEATURE_DYNAMIC_CODE /DFEATURE_CACHED_INTERFACE_DISPATCH /DVERIFY_HEAP /DUSE_GC_INFO_DECODER /DGUIDEXOS_NATIVEAOT_C011EC19_UNWIND_GC_INFO$c20CompileDefine /I`"$nativeAotRoot\Runtime`" /I`"$nativeAotRoot\Runtime\windows`" /I`"$sourceRoot`" /I`"$sourceRoot\native`" /I`"$sourceRoot\gc`" /I`"$sourceRoot\gc\env`" /I`"$nativeAotRoot\Runtime\inc`" /I`"$nativeAotRoot\Runtime\eventpipe`" /I`"$(Join-Path $root 'tools\dotnet\runtime-pack\src\platform')`" /I`"$palSourceRoot`" /FI`"$sourceRoot\gc\env\common.h`" /Fo:`"$coffNativeCodeManagerObj`" `"$coffNativeCodeManagerSource`"`nif errorlevel 1 exit /b %errorlevel%" } else { "" })
$(if ($isC011EC21) { "cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro /DWIN32 /D_WIN32 /D_WIN64 /DHOST_AMD64 /DTARGET_AMD64 /DTARGET_64BIT /DHOST_64BIT /DHOST_WINDOWS /DTARGET_WINDOWS /I`"$(Join-Path $root 'tools\dotnet\runtime-pack\src\platform')`" /Fo:`"$platformContractC21Obj`" `"$platformContractSource`"`nif errorlevel 1 exit /b %errorlevel%" } else { "" })
exit /b 0
"@
    }
    if ($isC011EC26) {
        $c011ec18CompileText = Get-Content -LiteralPath $c011ec18CompileBat -Raw
        $c011ec18CompileText = $c011ec18CompileText.Replace(
            "exit /b 0",
            $c26ThreadCompileLine + [Environment]::NewLine + "exit /b 0")
        Set-Content -LiteralPath $c011ec18CompileBat -Value $c011ec18CompileText -Encoding ASCII
    }
    $probeObjectPropertyArgument = if ($isAllocationContextFixupRootBoundary) {
        "-p:HostLogProofRuntimePackProbeObj=$probeObj"
    } else {
        ""
    }
    $managedProofMode = if ($isC011EC33) { "LifetimeTransition" } elseif ($isC011EC31) { "ShortWeakLive" } elseif ($isC011EC32) { "ShortWeakDead" } elseif ($isFirstRootFirstNonNullOldO) { "FirstNonNullOldO" } elseif ($isFirstNonNullRoot -or $isFirstRootCallbackEntry) { "FirstNonNullRoot" } else { "FirstCollectionBoundary" }
    $managedRuntimePackProperty = if ($isTransitionFrameControlPc -or $isC011EC19) {
        "-p:HostLogProofRuntimePackObj=$managedRuntimePackObj"
    } else {
        "-p:HostLogProofRuntimePackObj=$platformObj"
    }
    $managedRuntimePackAssembly = if ($isTransitionFrameControlPc -or $isC011EC19) {
        $managedRuntimePackContract = if ($isC011EC21) { ' "' + $platformContract + '"' } else { "" }
@"
lib.exe /nologo /OUT:"$managedRuntimePackObj" "$platformObj" "$nativeUnwindPrimitiveObj" "$allocFastPublicObj"$managedRuntimePackContract
if errorlevel 1 exit /b %errorlevel%
"@
    } else {
        ""
    }
    $artifactBat = Write-Batch "build-single-thread-suspend-ee-artifact.bat" @"
@echo off
setlocal
call "$vsBat" >nul
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /TC /c /GS- /Zl /Fo:"$runtimeSupportObj" "$runtimeSupportSource"
if errorlevel 1 exit /b %errorlevel%
$managedRuntimePackAssembly
"$dotnet" publish "$(Join-Path $root 'samples\managed\HostLogProof\HostLogProof.csproj')" -c Release -r win-x64 --self-contained true -p:PublishAot=true -p:InvariantGlobalization=true -p:IlcGenerateStackTraceData=false -p:IlcUseEnvironmentalTools=true -p:HostLogProofRuntimeSupportObj=$runtimeSupportObj -p:HostLogProofMapPath=$mapPath -p:HostLogProofMode=$managedProofMode -p:BaseOutputPath=$managedPublishRoot\bin\ -p:BaseIntermediateOutputPath=$managedPublishRoot\obj\ $managedRuntimePackProperty $probeObjectPropertyArgument -p:IlcSdkPath=$oldArtifact\sdk\
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro /DGUIDEXOS_NATIVEAOT_MANAGED_IMAGE /I"$(Join-Path $root 'tools\dotnet\runtime-pack\src\platform')" /I"$nativeAotRoot\Runtime" /Fo:"$startupProbeObj" "$startupProbeSource"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro /Fo:"$hostShimObj" "$hostShimSource"
if errorlevel 1 exit /b %errorlevel%
exit /b 0
"@
    $gcEnumArchiveArgs = if ($isCandidateLoadEnumeration) {
        "/REMOVE:`"nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\GcEnum.cpp.obj`" `"$gcEnum`""
    } else {
        ""
    }
    $gcWksArchiveArgs = if ($isFirstRootCallbackEntry) {
        "/REMOVE:`"nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\__\__\gc\gcwks.cpp.obj`" `"$gcWks`""
    } else {
        ""
    }
    $objectHandleArchiveArgs = if ($isC011EC29 -and -not $isC011EC30) {
        "/REMOVE:`"nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\__\__\gc\objecthandle.cpp.obj`" `"$objectHandleObj`""
    } else {
        ""
    }
    $objectHandleC30ArchiveArgs = if ($isC011EC30) {
        "/REMOVE:`"nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\__\__\gc\objecthandle.cpp.obj`" `"$objectHandleC30Obj`""
    } else {
        ""
    }
    $handleTableScanArchiveArgs = if ($isC011EC30) {
        "/REMOVE:`"nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\__\__\gc\handletablescan.cpp.obj`" `"$handleTableScanObj`""
    } else {
        ""
    }
    $handleTableHelpersArchiveArgs = if ($isC011EC31 -or $isC011EC32) {
        $handleTableHelpersArchiveObj = if ($isC011EC33) { $handleTableHelpersC33Obj } elseif ($isC011EC32) { $handleTableHelpersC32Obj } else { $handleTableHelpersC31Obj }
        "/REMOVE:`"nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\HandleTableHelpers.cpp.obj`" `"$handleTableHelpersArchiveObj`""
    } else {
        ""
    }
    $c011ec18ArchiveArgs = if ($isTransitionFrameControlPc -or $isC011EC19) {
        $coffArgs = if ($isC011EC19) { " /REMOVE:`"nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\windows\CoffNativeCodeManager.cpp.obj`" `"$coffNativeCodeManagerObj`"" } else { "" }
        "/REMOVE:`"nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\GCHelpers.cpp.obj`" /REMOVE:`"nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\StackFrameIterator.cpp.obj`" `"$stackFrameIteratorObj`"$coffArgs"
    } else {
        ""
    }
    $archiveAllocFastObj = if ($isTransitionFrameControlPc -or $isC011EC19) { $allocFastLinkObj } else { $allocFastObj }
    $archiveThreadObj = if ($isC011EC26) { $threadC011EC26Obj } else { $threadObj }
    $linkGcHelpersObj = if ($isTransitionFrameControlPc -or $isC011EC19) { $gcHelpersC011EC18Obj } else { $gcHelpersDiagnostic }
    $archiveBat = Write-Batch "build-single-thread-suspend-ee-gc-archive.bat" @"
@echo off
setlocal
call "$vsBat" >nul
if errorlevel 1 exit /b %errorlevel%
 lib.exe /nologo /OUT:"$adaptedArchive" "$activeArchive" /REMOVE:"$(Join-Path $root 'out\dotnet\pal-runtime-active-replacement-build\PalRedhawkMinWin.cpp.obj')" /REMOVE:"$(Join-Path $root 'out\dotnet\pal-runtime-active-replacement-build\thread.cpp.obj')" /REMOVE:"$(Join-Path $root 'out\dotnet\pal-runtime-active-replacement-build\guidexos_nativeaot_pal_contract.obj')" /REMOVE:"nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\__\__\gc\windows\gcenv.windows.cpp.obj" /REMOVE:"nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\gcenv.ee.cpp.obj" /REMOVE:"nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\amd64\AllocFast.asm.obj" /REMOVE:"nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\EHHelpers.cpp.obj" "$palBridge" "$palStartup" "$(Join-Path $runtimeRoot 'guidexos_gcenv.single-thread-suspend-ee.obj')" "$gcEnvEe" "$gcBridgeBoundary" "$platformContract" "$archiveThreadObj" "$ehObj" "$archiveAllocFastObj" "$probeObj" $gcWksArchiveArgs $gcEnumArchiveArgs $objectHandleArchiveArgs $objectHandleC30ArchiveArgs $handleTableScanArchiveArgs $handleTableHelpersArchiveArgs $c011ec18ArchiveArgs
if errorlevel 1 exit /b %errorlevel%
exit /b 0
"@
    $linkBat = Write-Batch "link-single-thread-suspend-ee.bat" @"
@echo off
call "$vsBat" >nul
link.exe /nologo /MANIFEST:NO /INCREMENTAL:NO /fixed /base:0x10000000 /SUBSYSTEM:NATIVE /ENTRY:GuideXosNativeAotGcStartupMain /OUT:"$pePath" /MAP:"$mapPath" /INCLUDE:RhInitialize /MERGE:.managedcode=.text /MERGE:.managed=.text /MERGE:hydrated=.bss /EXPORT:GuideXosNativeAotGcStartupMain /EXPORT:GuideXosNativeAotGcStartupInstallPalHooks /EXPORT:GuideXosNativeAotGcStartupInstallHookTable /EXPORT:GuideXosNativeAotGcStartupInstallPlatformHooks /EXPORT:GuideXosNativeAotGcStartupGetState /EXPORT:GuideXosNativeAotGcStartupGetPreGcState /EXPORT:GuideXosNativeAotGcStartupGetAllocationCount /EXPORT:GuideXosNativeAotGcStartupGetLastAllocationSize /EXPORT:GuideXosNativeAotGcStartupGetDiagnosticStage /EXPORT:ManagedMain /EXPORT:guideXosManagedAllocationBeginFirstCollectionBoundaryExperiment /EXPORT:guideXosManagedAllocationFinalize /EXPORT:guideXosManagedAllocationGetDiagnostics /EXPORT:guideXosManagedAllocationValidateObject /EXPORT:guideXosManagedAllocationRecordSentinelValidation /EXPORT:guideXosManagedAllocationGetLoopStatus /EXPORT:guideXosManagedAllocationGetHardLimit /NODEFAULTLIB:libucrt.lib /DEFAULTLIB:ucrt.lib /IGNORE:4104 "$managedPublishRoot\obj\x64\Release\net9.0\win-x64\native\HostLogProof.obj" "$runtimeSupportObj" "$platformObj" "$nativeUnwindPrimitiveObj" "$oldArtifact\sdk\bootstrapper.obj" "$adaptedArchive" "$startupProbeObj" "$hostShimObj" "$startupDiagnostic" "$linkGcHelpersObj" "$gcHelpersAlign" "$oldArtifact\sdk\eventpipe-disabled.lib" "$oldArtifact\sdk\Runtime.VxsortEnabled.lib" "$oldArtifact\sdk\standalonegc-disabled.lib" "$oldArtifact\sdk\zlibstatic.lib" "$oldArtifact\sdk\System.Globalization.Native.Aot.lib" "$oldArtifact\sdk\System.IO.Compression.Native.Aot.lib"
exit /b %errorlevel%
"@

    if (-not $SkipManagedBuild) {
        $stalePaths = @($platformObj,$nativeUnwindPrimitiveObj,$gcEnvEe,$probeObj,$gcBridgeBoundary,$runtimeSupportObj,$hostShimObj,$startupProbeObj,$adaptedArchive,$pePath,$elfPath,$mapPath)
        if ($isC011EC26) { $stalePaths += $threadC011EC26Obj }
        if ($isCandidateLoadEnumeration) { $stalePaths += $gcEnum }
        if ($isFirstRootCallbackEntry) { $stalePaths += $gcWks }
        if ($isTransitionFrameControlPc -or $isC011EC19) { $stalePaths += @($gcHelpersC011EC18Obj,$stackFrameIteratorObj,$managedRuntimePackObj,$nativeUnwindPrimitiveObj) }
        if ($isC011EC19) { $stalePaths += $coffNativeCodeManagerObj }
        if ($isC011EC29 -and -not $isC011EC30) { $stalePaths += $objectHandleObj }
        if ($isC011EC30) { $stalePaths += $objectHandleC30Obj }
        if ($isC011EC30) { $stalePaths += $handleTableScanObj }
        if ($isC011EC31) { $stalePaths += $handleTableHelpersC31Obj }
        if ($isC011EC32 -and -not $isC011EC33) { $stalePaths += $handleTableHelpersC32Obj }
        if ($isC011EC33) { $stalePaths += $handleTableHelpersC33Obj }
        foreach ($stale in $stalePaths) {
            if (Test-Path -LiteralPath $stale -PathType Leaf) { Remove-Item -LiteralPath $stale -Force }
        }
        Invoke-Batch $runtimeBat "runtime-pack-build.log"
        if ($isTransitionFrameControlPc -or $isC011EC19) { Invoke-Batch $c011ec18CompileBat "c011ec18-instrumentation-build.log" }
        Invoke-Batch $artifactBat "managed-artifact-build.log"
        Invoke-Batch $archiveBat "gc-archive-build.log"
        Invoke-Batch $linkBat "managed-link.log"
    }
    $requiredBuildOutputs = @($platformObj,$gcEnvEe,$probeObj,$runtimeSupportObj,$hostShimObj,$startupProbeObj,$adaptedArchive,$pePath,$mapPath)
    if ($isCandidateLoadEnumeration) { $requiredBuildOutputs += $gcEnum }
    if ($isFirstRootCallbackEntry) { $requiredBuildOutputs += $gcWks }
    if ($isC011EC29 -and -not $isC011EC30) { $requiredBuildOutputs += $objectHandleObj }
    if ($isC011EC30) { $requiredBuildOutputs += $objectHandleC30Obj }
    if ($isC011EC30) { $requiredBuildOutputs += $handleTableScanObj }
    if ($isC011EC31) { $requiredBuildOutputs += $handleTableHelpersC31Obj }
    if ($isC011EC32 -and -not $isC011EC33) { $requiredBuildOutputs += $handleTableHelpersC32Obj }
    if ($isC011EC33) { $requiredBuildOutputs += $handleTableHelpersC33Obj }
    if ($isTransitionFrameControlPc -or $isC011EC19) { $requiredBuildOutputs += @($gcHelpersC011EC18Obj,$stackFrameIteratorObj,$allocFastPublicObj,$managedRuntimePackObj,$nativeUnwindPrimitiveObj) }
    if ($isC011EC19) { $requiredBuildOutputs += $coffNativeCodeManagerObj }
    foreach ($path in $requiredBuildOutputs) { Require-File $path "Single-thread SuspendEE build output" }
    Invoke-LoggedCommand $python @($converter,$pePath,$elfPath,"--map",$mapPath,"--symbol","ManagedMain") (Join-Path $runRoot "pe-to-elf.log")
    Require-File $elfPath "Single-thread SuspendEE ELF"
    Invoke-LoggedCommand $objdump @("-p",$pePath) (Join-Path $runRoot "pe-imports.txt")
    Invoke-LoggedCommand $readelf @("-h","-l","-S","-r","-s","-d",$elfPath) (Join-Path $runRoot "elf-inspection.txt")
    $imports = Get-Content -LiteralPath (Join-Path $runRoot "pe-imports.txt") -Raw
    if ($imports -match 'FlsGetValue|FlsSetValue') { throw "Single-thread SuspendEE PE still exposes live Windows FLS imports." }
    $mapText = Get-Content -LiteralPath $mapPath -Raw
    $callbackSymbolName = '?Promote@GCHeap@WKS@@SAXPEAPEAVObject@@PEAUScanContext@@I@Z'
    $callbackAddressText = if ($isFirstRootCallbackEntry) { Extract-MapAddress $mapText $callbackSymbolName } else { $null }
    $markHelperSymbolName = '?mark_object_simple@gc_heap@WKS@@CAXPEAPEAE@Z'
    $markHelperAddressText = if ($isFirstRootPreMarkBoundary) { Extract-MapAddress $mapText $markHelperSymbolName } else { $null }
    if ($isFirstRootCallbackEntry -and -not $isC011EC30) {
        Invoke-LoggedCommand $objdump @('-d','-Mintel',$pePath) (Join-Path $runRoot 'artifact-disassembly.txt')
        if ($false) {
            Assert-Text $validationText '\[nativeaot-gc-first-root-first-non-null-old-o\] SAFE_STOP marker=C011EC14' "C011EC14 first non-null old_o marker"
            $nonNullOldOLine = (($validationText -split "`n") | Where-Object { $_ -match '\[nativeaot-gc-first-root-first-non-null-old-o\] SAFE_STOP marker=C011EC14' } | Select-Object -Last 1)
            if ([string]::IsNullOrWhiteSpace($nonNullOldOLine)) { throw "C011EC14 marker line was not isolated in $name." }
            Assert-Text $nonNullOldOLine 'queueInsertions=00000011 queueWrites=00000011 queueHistoryOverflow=00000000' "17 real queue insertions"
            Assert-Text $nonNullOldOLine 'nullDecisions=00000010 nonNullDecisions=00000001 decisionRequests=00000011 decisionEntries=00000011' "16 null decisions followed by the first non-null decision"
            Assert-Text $nonNullOldOLine 'markedRequests=00000001 markedEntries=00000001 markedReturns=00000001 markStateReads=00000001 rawMarkWordReads=00000001' "one actual marked(old_o) read and return"
            Assert-Text $nonNullOldOLine 'newMutationAttempts=00000000 newMutationExecutions=00000000 markBitWrites=00000000 graphTraversal=00000000 childReferenceReads=00000000 childObjectsDiscovered=00000000 secondObjectMarkAttempts=00000000' "stopped before set_marked, traversal, or a second object"
            Assert-Text $nonNullOldOLine 'restart=00000000 resume=00000000' "no RestartEE or managed resume"
            Assert-Text $nonNullOldOLine 'lockHeld=00000001 eeSuspended=00000001 managedEntryProhibited=00000001' "suspended single-mutator invariants"
            Assert-Text $nonNullOldOLine 'sentinelFailures=00000000 objectValidationBeforeFixup=00000000 objectValidationAfterFixup=00000000 duplicateObjectAddresses=00000000 objectHistoryOverflow=00000000' "real-object and sentinel validation"
            $oldRootSlot = Get-MarkerField $nonNullOldOLine 'rootSlot'
            $oldRawRoot = Get-MarkerField $nonNullOldOLine 'rawRoot'
            $oldStorage = Get-MarkerField $nonNullOldOLine 'storageObject'
            $oldSentinel = Get-MarkerField $nonNullOldOLine 'sentinel'
            $oldObject = Get-MarkerField $nonNullOldOLine 'old_o'
            $oldSlot = Get-MarkerField $nonNullOldOLine 'slotOld'
            $newSlot = Get-MarkerField $nonNullOldOLine 'slotNew'
            if ($oldRootSlot -eq '0x0000000000000000' -or $oldRawRoot -eq '0x0000000000000000' -or $oldStorage -eq '0x0000000000000000' -or $oldSentinel -eq '0x0000000000000000') { throw "C011EC14 did not record non-null managed root identity in $name." }
            if ($oldObject -eq '0x0000000000000000' -or $oldObject -ne $oldSlot -or $newSlot -eq '0x0000000000000000' -or $oldObject -eq $oldSentinel) { throw "C011EC14 selected old_o identity was not a real managed object in $name." }
            if ((Get-MarkerField $nonNullOldOLine 'slotIndex') -ne '0x0000000000000000' -or (Get-MarkerField $nonNullOldOLine 'cursorBefore') -ne '0x0000000000000000' -or (Get-MarkerField $nonNullOldOLine 'cursorAfter') -ne '0x0000000000000001') { throw "C011EC14 did not capture the ring wrap at slot zero in $name." }
            if ((Get-MarkerField $nonNullOldOLine 'provenanceValid') -ne '0x00000001' -or (Get-MarkerField $nonNullOldOLine 'findRange') -ne '0x00000001' -or (Get-MarkerField $nonNullOldOLine 'heapMembership') -ne '0x00000001') { throw "C011EC14 old_o provenance did not resolve to the real object history/range in $name." }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker="C011EC14"; outcome="A"; harnessTerminated=$true
                root=[ordered]@{slot=$oldRootSlot;rawValue=$oldRawRoot;storageObject=$oldStorage;sentinel=$oldSentinel}
                queue=[ordered]@{insertions=(Get-MarkerField $nonNullOldOLine 'queueInsertions');writes=(Get-MarkerField $nonNullOldOLine 'queueWrites');slotAddress=(Get-MarkerField $nonNullOldOLine 'selectedSlot');slotIndex=(Get-MarkerField $nonNullOldOLine 'slotIndex');oldSlot=$oldSlot;newSlot=$newSlot;oldObject=$oldObject;cursorBefore=(Get-MarkerField $nonNullOldOLine 'cursorBefore');cursorAfter=(Get-MarkerField $nonNullOldOLine 'cursorAfter');base=(Get-MarkerField $nonNullOldOLine 'queueBase')}
                decision=[ordered]@{requests=(Get-MarkerField $nonNullOldOLine 'decisionRequests');entries=(Get-MarkerField $nonNullOldOLine 'decisionEntries');nullDecisions=(Get-MarkerField $nonNullOldOLine 'nullDecisions');nonNullDecisions=(Get-MarkerField $nonNullOldOLine 'nonNullDecisions');oldObject=$oldObject;returnAddress=(Get-MarkerField $nonNullOldOLine 'decisionReturnAddress')}
                markState=[ordered]@{requests=(Get-MarkerField $nonNullOldOLine 'markedRequests');entries=(Get-MarkerField $nonNullOldOLine 'markedEntries');returns=(Get-MarkerField $nonNullOldOLine 'markedReturns');reads=(Get-MarkerField $nonNullOldOLine 'markStateReads');result=(Get-MarkerField $nonNullOldOLine 'markedResult');rawReads=(Get-MarkerField $nonNullOldOLine 'rawMarkWordReads');rawHeader=(Get-MarkerField $nonNullOldOLine 'rawHeader');mask=(Get-MarkerField $nonNullOldOLine 'markMask')}
                provenance=[ordered]@{valid=(Get-MarkerField $nonNullOldOLine 'provenanceValid');findRange=(Get-MarkerField $nonNullOldOLine 'findRange');heapMembership=(Get-MarkerField $nonNullOldOLine 'heapMembership');generation=(Get-MarkerField $nonNullOldOLine 'generation');objectHistoryIndex=(Get-MarkerField $nonNullOldOLine 'objectHistoryIndex')}
                counters=[ordered]@{callbacks=(Get-MarkerField $nonNullOldOLine 'callbacks');candidateLoads=(Get-MarkerField $nonNullOldOLine 'candidateLoads');markHelpers=(Get-MarkerField $nonNullOldOLine 'markHelpers');callbackReturnsBeforeDecision=(Get-MarkerField $nonNullOldOLine 'callbackReturnsBeforeDecision');callbackReturns=(Get-MarkerField $nonNullOldOLine 'callbackReturns');restart=(Get-MarkerField $nonNullOldOLine 'restart');resume=(Get-MarkerField $nonNullOldOLine 'resume')}
                mutation=[ordered]@{newAttempts=(Get-MarkerField $nonNullOldOLine 'newMutationAttempts');newExecutions=(Get-MarkerField $nonNullOldOLine 'newMutationExecutions');markBitWrites=(Get-MarkerField $nonNullOldOLine 'markBitWrites')}
                traversal=[ordered]@{graph=(Get-MarkerField $nonNullOldOLine 'graphTraversal');childReferenceReads=(Get-MarkerField $nonNullOldOLine 'childReferenceReads');childObjects=(Get-MarkerField $nonNullOldOLine 'childObjectsDiscovered');secondObjectMarkAttempts=(Get-MarkerField $nonNullOldOLine 'secondObjectMarkAttempts')}
                threadStore=[ordered]@{lockHeld=(Get-MarkerField $nonNullOldOLine 'lockHeld');eeSuspended=(Get-MarkerField $nonNullOldOLine 'eeSuspended');managedEntryProhibited=(Get-MarkerField $nonNullOldOLine 'managedEntryProhibited');restart=(Get-MarkerField $nonNullOldOLine 'restart');resume=(Get-MarkerField $nonNullOldOLine 'resume')}
                validation=[ordered]@{sentinelFailures=(Get-MarkerField $nonNullOldOLine 'sentinelFailures');objectHistoryOverflow=(Get-MarkerField $nonNullOldOLine 'objectHistoryOverflow')}
            }
        } elseif ($isFirstRootPostQueueMarkDecision) {
            $disassemblyText = Get-Content -LiteralPath (Join-Path $runRoot 'artifact-disassembly.txt') -Raw
            $markHelperNumeric = [Convert]::ToUInt64($markHelperAddressText, 16)
            $markHelperHex = $markHelperNumeric.ToString('x')
            $helperStartMatch = [regex]::Match($disassemblyText, '(?im)^\s*' + [regex]::Escape($markHelperHex) + ':')
            $helperStart = if ($helperStartMatch.Success) { $helperStartMatch.Index } else { -1 }
            if ($helperStart -lt 0) { throw "Disassembly did not contain the linked mark_object_simple helper for C011EC13." }
            $helperTail = $disassemblyText.Substring($helperStart)
            $helperLines = $helperTail -split "`r?`n"
            $firstMutationLine = $helperLines | Where-Object { $_ -match '(?i)^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2}\s+)+mov\s+QWORD PTR\s+\[(?!rsp|rip)[^]]+\],\w+\s*$' } | Select-Object -First 1
            if ([string]::IsNullOrWhiteSpace($firstMutationLine)) { throw "C011EC13 disassembly did not expose the inherited queue slot write." }
            $firstMutationAddress = ([regex]::Match($firstMutationLine, '(?i)^\s*(?<address>[0-9a-f]+):').Groups['address'].Value)
            $firstMutationIndex = [Array]::IndexOf($helperLines, $firstMutationLine)
            $cursorLine = $helperLines[($firstMutationIndex + 1)..($helperLines.Count - 1)] | Where-Object { $_ -match '(?i)^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2}\s+)+mov\s+QWORD PTR\s+\[rip\+[^]]+\],\w+' } | Select-Object -First 1
            if ([string]::IsNullOrWhiteSpace($cursorLine)) { throw "C011EC13 disassembly did not expose the inherited queue cursor write." }
            $cursorAddress = ([regex]::Match($cursorLine, '(?i)^\s*(?<address>[0-9a-f]+):').Groups['address'].Value)
            $cursorIndex = [Array]::IndexOf($helperLines, $cursorLine)
            if ($cursorIndex -lt 0) { throw "C011EC13 cursor write was not located inside mark_object_simple disassembly." }
            $nullHelperAddressText = Extract-MapAddress $mapText 'guideXosNativeAotFirstRootPostQueueNullDecisionCompleted'
            $nullHelperHex = ([Convert]::ToUInt64($nullHelperAddressText, 16)).ToString('x')
            $nullCallLine = $null
            if ($cursorIndex -lt ($helperLines.Count - 1)) {
                $nullCallLine = $helperLines[($cursorIndex + 1)..($helperLines.Count - 1)] | Where-Object { $_ -match '(?i)\bcall\s+0x0*' + [regex]::Escape($nullHelperHex) + '\b' } | Select-Object -First 1
            }
            if ([string]::IsNullOrWhiteSpace($nullCallLine)) { throw "C011EC13 disassembly did not expose the null-decision safe-stop call." }
            $nullCallAddress = ([regex]::Match($nullCallLine, '(?i)^\s*(?<address>[0-9a-f]+):').Groups['address'].Value)
            $nullCallIndex = [Array]::IndexOf($helperLines, $nullCallLine)
            if ($nullCallIndex -lt 0) { throw "C011EC13 null-decision call was not located inside mark_object_simple disassembly." }
            $nullTargetIndex = $nullCallIndex - 1
            while ($nullTargetIndex -ge 0 -and $helperLines[$nullTargetIndex] -notmatch '(?i)\bxor\s+ecx,ecx\b') { $nullTargetIndex-- }
            if ($nullTargetIndex -lt 0) { throw "C011EC13 disassembly did not expose the null-decision continuation target." }
            $nullBranchTargetText = ([regex]::Match($helperLines[$nullTargetIndex], '(?i)^\s*(?<address>[0-9a-f]+):').Groups['address'].Value)
            $branchLine = $helperLines[($cursorIndex + 1)..($nullTargetIndex - 1)] | Where-Object { $_ -match '(?i)\b(?:je|jz|jne|jnz)\s+0x0*' + [regex]::Escape($nullBranchTargetText) + '\b' } | Select-Object -First 1
            if ([string]::IsNullOrWhiteSpace($branchLine)) { throw "C011EC13 disassembly did not expose the old_o null branch target." }
            $branchIndex = [Array]::IndexOf($helperLines, $branchLine)
            $testLookbackStart = [Math]::Max(0, $branchIndex - 12)
            $testLookbackEnd = [Math]::Max(0, $branchIndex - 1)
            $nullTestLine = @($helperLines[$testLookbackStart..$testLookbackEnd]) | Where-Object { $_ -match '(?i)\b(?:test|cmp)\b' } | Select-Object -Last 1
            $falseHelperAddressText = Extract-MapAddress $mapText 'guideXosNativeAotFirstRootPostQueueMarkedFalseBoundary'
            $falseHelperHex = ([Convert]::ToUInt64($falseHelperAddressText, 16)).ToString('x')
            $nextMutationLine = $null
            if ($branchIndex -lt ($nullTargetIndex - 1)) {
                $falseCallLine = $helperLines[($branchIndex + 1)..($nullTargetIndex - 1)] | Where-Object { $_ -match '(?i)\bcall\s+0x0*' + [regex]::Escape($falseHelperHex) + '\b' } | Select-Object -First 1
            }
            if (-not [string]::IsNullOrWhiteSpace($falseCallLine)) {
                $falseCallIndex = [Array]::IndexOf($helperLines, $falseCallLine)
                if ($falseCallIndex -ge 0) {
                    $nextMutationLine = $helperLines[($falseCallIndex + 1)..[Math]::Min($helperLines.Count - 1, $falseCallIndex + 20)] | Where-Object { $_ -match '(?i)^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2}\s+)+(?:or\s+QWORD PTR\s+\[(?!rsp|rip)[^]]+\],0x1\s*$)' } | Select-Object -First 1
                }
            }
            $nextMutationAddress = if ($nextMutationLine) { ([regex]::Match($nextMutationLine, '(?i)^\s*(?<address>[0-9a-f]+):').Groups['address'].Value) } else { $null }
            Set-Content -LiteralPath (Join-Path $runRoot 'post-queue-machine-code.txt') -Value @(
                "markHelperSymbol=$markHelperSymbolName",
                "markHelperAddress=0x$markHelperAddressText",
                "inheritedQueueSlotWriteInstruction=0x$($firstMutationAddress.ToUpperInvariant())",
                "inheritedQueueSlotWriteLine=$firstMutationLine",
                "inheritedCursorWriteInstruction=0x$($cursorAddress.ToUpperInvariant())",
                "inheritedCursorWriteLine=$cursorLine",
                "nullDecisionHelper=guideXosNativeAotFirstRootPostQueueNullDecisionCompleted",
                "nullDecisionHelperAddress=0x$nullHelperAddressText",
                "nullDecisionCallSite=0x$($nullCallAddress.ToUpperInvariant())",
                "nullDecisionCallLine=$nullCallLine",
                "nullTestInstruction=$nullTestLine",
                "nullBranchInstruction=$branchLine",
                "markedRepresentation=inline marked(old_o) -> header(old_o)->IsMarked() -> RawGetMethodTable() & GC_MARKED",
                "markedOutOfLineHelper=none",
                "nextMutationHelper=inline mark_queue_t::queue_mark set_marked(old_o) at locked gc.cpp:27333",
                "nextMutationInstruction=$(if ($nextMutationAddress) { '0x' + $nextMutationAddress.ToUpperInvariant() } else { 'not-emitted; false boundary is non-returning at runtime' })",
                "nextMutationLine=$nextMutationLine",
                "source=locked gc.cpp:27316-27335 queue_mark; old_o capture, slot/cursor writes, null decision, then marked(old_o)",
                "safeStop=inside the source-valid old_o == nullptr branch after null result and before marked(old_o); no mark-state read or later mutation"
            ) -Encoding ASCII
        } elseif ($isFirstRootFirstMarkMutation) {
            $disassemblyText = Get-Content -LiteralPath (Join-Path $runRoot 'artifact-disassembly.txt') -Raw
            $markHelperNumeric = [Convert]::ToUInt64($markHelperAddressText, 16)
            $markHelperHex = $markHelperNumeric.ToString('x')
            $helperStartMatch = [regex]::Match($disassemblyText, '(?im)^\s*' + [regex]::Escape($markHelperHex) + ':')
            $helperStart = if ($helperStartMatch.Success) { $helperStartMatch.Index } else { -1 }
            if ($helperStart -lt 0) { throw "Disassembly did not contain the linked mark_object_simple helper for C011EC12." }
            $helperTail = $disassemblyText.Substring($helperStart)
            $helperLines = $helperTail -split "`r?`n"
            $firstMutationLine = $helperLines | Where-Object { $_ -match '(?i)^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2}\s+)+mov\s+QWORD PTR\s+\[rdi\],\w+\s*$' } | Select-Object -First 1
            if ([string]::IsNullOrWhiteSpace($firstMutationLine)) {
                $firstMutationLine = $helperLines | Where-Object { $_ -match '(?i)^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2}\s+)+mov\s+QWORD PTR\s+\[[^]]+\],\w+\s*$' } | Select-Object -First 1
            }
            if ([string]::IsNullOrWhiteSpace($firstMutationLine)) { throw "C011EC12 disassembly did not expose the first queue slot write." }
            $firstMutationAddress = ([regex]::Match($firstMutationLine, '(?i)^\s*(?<address>[0-9a-f]+):').Groups['address'].Value)
            $firstMutationInstructionText = '0x' + $firstMutationAddress.ToUpperInvariant()
            $firstMutationIndex = [Array]::IndexOf($helperLines, $firstMutationLine)
            $nextMutationLine = $helperLines[($firstMutationIndex + 1)..($helperLines.Count - 1)] | Where-Object { $_ -match '(?i)^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2}\s+)+mov\s+QWORD PTR\s+\[rip\+[^]]+\],\w+' } | Select-Object -First 1
            $nextMutationAddress = if ($nextMutationLine) { ([regex]::Match($nextMutationLine, '(?i)^\s*(?<address>[0-9a-f]+):').Groups['address'].Value) } else { $null }
            $nextMutationInstructionText = if ($nextMutationAddress) { '0x' + $nextMutationAddress.ToUpperInvariant() } else { $null }
            Set-Content -LiteralPath (Join-Path $runRoot 'first-mark-machine-code.txt') -Value @(
                "markHelperSymbol=$markHelperSymbolName",
                "markHelperAddress=0x$markHelperAddressText",
                "firstMutationInstruction=$firstMutationInstructionText",
                "firstMutationLine=$firstMutationLine",
                "nextMutationInstruction=$nextMutationInstructionText",
                "nextMutationLine=$nextMutationLine",
                "source=locked gc.cpp:27308-27335 queue_mark; slot_table[slot_index]=o then curr_slot_index advancement",
                "safeStop=after the two-write queue invariant and before the old_o null test/marked(old_o)"
            ) -Encoding ASCII
        } elseif ($isFirstRootPreMarkBoundary) {
            $disassemblyText = Get-Content -LiteralPath (Join-Path $runRoot 'artifact-disassembly.txt') -Raw
            $markHelperNumeric = [Convert]::ToUInt64($markHelperAddressText, 16)
            $markHelperHex = $markHelperNumeric.ToString('x')
            $callbackNumeric = [Convert]::ToUInt64($callbackAddressText, 16)
            $callPattern = '(?im)call\s+0x0*' + [regex]::Escape($markHelperHex) + '\b'
            $markCallMatches = [regex]::Matches($disassemblyText, $callPattern)
            $markCallAddress = $null
            foreach ($candidate in $markCallMatches) {
                $prefix = $disassemblyText.Substring(0, $candidate.Index)
                $addressMatches = [regex]::Matches($prefix, '(?im)^\s*(?<address>[0-9a-f]+):')
                if ($addressMatches.Count -eq 0) { continue }
                $candidateAddress = [Convert]::ToUInt64($addressMatches[$addressMatches.Count - 1].Groups['address'].Value, 16)
                if ($candidateAddress -gt $callbackNumeric) { $markCallAddress = $candidateAddress; break }
            }
            if ($null -eq $markCallAddress) { throw "Disassembly did not retain the out-of-line mark_object_simple call after the C011EC11 boundary." }
            $markCallSiteText = ('0x{0:X16}' -f $markCallAddress)
            $firstMutationInstructionText = ('0x{0:X16}' -f ($markHelperNumeric + 0x1D))
            $firstMutationPattern = '(?im)^\s*' + $firstMutationInstructionText.Substring(2).TrimStart('0') + ':.*mov\s+QWORD PTR\s+\[rbx\+rdx\*8\],rax'
            if ($disassemblyText -notmatch $firstMutationPattern) { throw "The expected MARK_PHASE_PREFETCH first mutation instruction was not present at $firstMutationInstructionText." }
            Set-Content -LiteralPath (Join-Path $runRoot 'pre-mark-machine-code.txt') -Value @(
                "markHelperSymbol=$markHelperSymbolName",
                "markHelperAddress=0x$markHelperAddressText",
                "markCallSite=$markCallSiteText",
                "firstMutationInstruction=$firstMutationInstructionText",
                "source=locked gc.cpp:27308-27335 queue_mark; MARK_PHASE_PREFETCH slot_table[slot_index]=o precedes marked(old_o)",
                "promoteSource=locked gc.cpp:49474-49544; boundary before gc.cpp:49541 mark_object_simple invocation"
            ) -Encoding ASCII
        }
        Set-Content -LiteralPath (Join-Path $runRoot 'callback-symbol.txt') -Value @(
            "symbol=$callbackSymbolName",
            "callbackAddress=0x$callbackAddressText",
            "source=locked src/coreclr/gc/gc.cpp:49474-49544, included by src/coreclr/gc/gcwks.cpp",
            "typedef=promote_func(Object**, ScanContext*, uint32_t)",
            "callingConvention=Microsoft x64 AMD64 (RCX, RDX, R8; RAX return unused; 32-byte shadow space)"
        ) -Encoding ASCII
    }
    $requiredSymbols = @("ManagedMain","RhpNewArray","RhpNewArrayRare","RhpGcAlloc","guideXosManagedAllocationBeginFirstCollectionBoundaryExperiment","guideXosNativeAotSuspendEeEntry","guideXosNativeAotSuspendEeAfterLock","guideXosNativeAotSuspendEeAfterSuspend","guideXosNativeAotSuspendEeBodyReturn","guideXosNativeAotDisablePreemptiveEntry","guideXosNativeAotDisablePreemptiveReturn","guideXosManagedAllocationGetDiagnostics")
    if ($isFirstRootHeapResolutionOrCondemned -or $isFirstRootMembershipClassification) {
        $requiredSymbols += @("guideXosNativeAotAllocationContextFixupRequest","guideXosNativeAotAllocationContextFixupGcStartWorkObserver","guideXosNativeAotAllocationRootPhaseRequested","guideXosNativeAotAllocationContextFixupEnumerationEntry","guideXosNativeAotAllocationContextFixupContextVisited","guideXosNativeAotAllocationContextFixupEnumerationComplete","guideXosNativeAotFirstPerThreadRootGcScanRootsEntered","guideXosNativeAotFirstPerThreadRootForeachThreadEntered","guideXosNativeAotFirstPerThreadRootIteratorInitialized","guideXosNativeAotFirstPerThreadRootIteratorCompletion","guideXosNativeAotFirstPerThreadRootThreadEnumerated","guideXosNativeAotFirstPerThreadRootThreadExcluded","guideXosNativeAotFirstPerThreadRootThreadIncluded","guideXosNativeAotFirstPerThreadRootThreadStaticListObserved","guideXosNativeAotFirstPerThreadRootThreadStaticStorageEntered","guideXosNativeAotFirstNonNullRootCandidateLoadRequested","guideXosNativeAotFirstNonNullRootCandidateMachineWordLoaded","guideXosNativeAotFirstRootCallbackCallSiteEntered","guideXosNativeAotFirstRootCallbackEntered","guideXosNativeAotFirstRootMembershipCandidateLoaded","guideXosNativeAotFirstRootMembershipCheckRequested","guideXosNativeAotFirstRootMembershipCheckEntered","guideXosNativeAotFirstRootMembershipCheckCompleted","guideXosNativeAotFirstRootMembershipResultBoundary","guideXosManagedThreadStaticProofAssigned","guideXosManagedThreadStaticProofReadback")
        if ($isFirstRootHeapResolutionOrCondemned) {
            $requiredSymbols += @("guideXosNativeAotFirstRootHeapResolutionRequested","guideXosNativeAotFirstRootHeapResolutionEntered","guideXosNativeAotFirstRootHeapResolutionCompleted")
        }
        if ($isFirstRootCondemnedGenerationDecisionOrPreMark) {
            $requiredSymbols += @("guideXosNativeAotFirstRootCondemnedGenerationDecisionRequested","guideXosNativeAotFirstRootCondemnedGenerationDecisionEntered","guideXosNativeAotFirstRootCondemnedGenerationQueryStart","guideXosNativeAotFirstRootCondemnedGenerationQueryCompleted","guideXosNativeAotFirstRootCondemnedGenerationSegmentLookupCompleted","guideXosNativeAotFirstRootCondemnedGenerationDecisionCompleted")
        }
        if ($isFirstRootPreMarkBoundary) {
            $requiredSymbols += @("guideXosNativeAotFirstRootPreMarkTrueBranchEntered","guideXosNativeAotFirstRootPreMarkRootFlagTest","guideXosNativeAotFirstRootPreMarkConservativeCheck","guideXosNativeAotFirstRootPreMarkDebugValidationEntered","guideXosNativeAotFirstRootPreMarkDebugValidationMethodTableRead","guideXosNativeAotFirstRootPreMarkDebugValidationHeapPointer","guideXosNativeAotFirstRootPreMarkDebugValidationCompleted","guideXosNativeAotFirstRootPreMarkBoundaryReached")
            if ($isFirstRootFirstMarkMutation) {
                $requiredSymbols += @("guideXosNativeAotFirstRootMarkHelperEntered","guideXosNativeAotFirstRootMarkWorklistWriteBefore","guideXosNativeAotFirstRootMarkWorklistWriteCompleted")
            } elseif ($isFirstRootPostQueueMarkDecision) {
                $requiredSymbols += @("guideXosNativeAotFirstRootMarkHelperEntered","guideXosNativeAotFirstRootMarkWorklistWriteBefore","guideXosNativeAotFirstRootPostQueueWorklistWriteCompleted","guideXosNativeAotFirstRootPostQueueDecisionRequested","guideXosNativeAotFirstRootPostQueueDecisionEntered","guideXosNativeAotFirstRootPostQueueNullDecisionCompleted","guideXosNativeAotFirstRootPostQueueNonNullDecisionStarted","guideXosNativeAotFirstRootPostQueueMarkedRequested","guideXosNativeAotFirstRootPostQueueMarkedCompleted","guideXosNativeAotFirstRootPostQueueMarkedTrueBoundary","guideXosNativeAotFirstRootPostQueueMarkedFalseBoundary")
            }
        }
    } elseif ($isStackProviderTransitionFailFast) {
        $requiredSymbols += @("guideXosNativeAotC011EC15GcScanRootsEntered","guideXosNativeAotC011EC15ProviderEntered","guideXosNativeAotC011EC15EnumGcRefReturned")
    } elseif ($isNextGenuineRootProvider) {
        $requiredSymbols += @("guideXosNativeAotAllocationContextFixupRequest","guideXosNativeAotAllocationContextFixupGcStartWorkObserver","guideXosNativeAotAllocationRootPhaseRequested","guideXosNativeAotAllocationContextFixupEnumerationEntry","guideXosNativeAotAllocationContextFixupContextVisited","guideXosNativeAotAllocationContextFixupEnumerationComplete","guideXosNativeAotFirstPerThreadRootGcScanRootsEntered","guideXosNativeAotFirstPerThreadRootForeachThreadEntered","guideXosNativeAotFirstPerThreadRootIteratorInitialized","guideXosNativeAotFirstPerThreadRootIteratorCompletion","guideXosNativeAotFirstPerThreadRootThreadEnumerated","guideXosNativeAotFirstPerThreadRootThreadExcluded","guideXosNativeAotFirstPerThreadRootThreadIncluded","guideXosNativeAotFirstPerThreadRootThreadStaticListObserved","guideXosNativeAotFirstPerThreadRootThreadStaticStorageEntered","guideXosNativeAotC011EC15GcScanRootsEntered","guideXosNativeAotC011EC15ProviderEntered","guideXosNativeAotC011EC15CandidateObserved","guideXosNativeAotC011EC15EnumGcRefReturned","guideXosNativeAotC011EC15QueueMarkReturned","guideXosNativeAotC011EC15MarkHelperReturned","guideXosNativeAotC011EC15PromoteReturned","guideXosNativeAotC011EC15PromoteEntered","guideXosNativeAotC011EC15PromoteCandidateLoaded")
        if (-not $isC011EC31 -and -not $isC011EC32) {
            $requiredSymbols += @("guideXosManagedThreadStaticProofAssigned","guideXosManagedThreadStaticProofReadback")
        }
    if ($isC011EC19) {
        $requiredSymbols += @("guideXosNativeAotC011EC18RhpGcAllocEntered","guideXosNativeAotC011EC18IteratorInitial","guideXosNativeAotC011EC18IteratorCodeManagerLookup","guideXosNativeAotC011EC18IteratorFindMethodInfo","guideXosNativeAotC011EC18IteratorFramePointer","guideXosNativeAotC011EC18IteratorUnwind","guideXosNativeAotC011EC19GcRootReported","guideXosNativeAotC011EC19UnwindEntered","guideXosNativeAotC011EC19UnwindMetadata","guideXosNativeAotC011EC19GcInfoLookup","guideXosNativeAotC011EC19GcInfoDecodeStarted","guideXosNativeAotC011EC19GcInfoInterruptibility","guideXosNativeAotC011EC19GcInfoDecodeCompleted")
            if (-not $isC011EC20) {
                $requiredSymbols += @("guideXosNativeAotC011EC19UnwindCompleted","guideXosNativeAotC011EC19SafeStop")
            }
            if ($isC011EC20) {
                $requiredSymbols += @("guideXosNativeAotC011EC20TransitionCrossed","guideXosNativeAotC011EC20UnwindInputs","guideXosNativeAotC011EC20CallerMethodInfo","guideXosNativeAotC011EC20UnwindCompleted")
                if (-not $isC011EC21) {
                    $requiredSymbols += "guideXosNativeAotC011EC20SafeStop"
                }
            }
            if ($isC011EC21 -and -not $isC011EC23) {
                $requiredSymbols += @("guideXosNativeAotC011EC21DescribeNativeCaller","guideXosNativeAotC011EC21SafeStop")
            }
        }
    } elseif ($isFirstRootCallbackEntry -and -not $isFirstRootFirstNonNullOldO) {
        $requiredSymbols += @("guideXosNativeAotAllocationContextFixupRequest","guideXosNativeAotAllocationContextFixupGcStartWorkObserver","guideXosNativeAotAllocationRootPhaseRequested","guideXosNativeAotAllocationContextFixupEnumerationEntry","guideXosNativeAotAllocationContextFixupContextVisited","guideXosNativeAotAllocationContextFixupEnumerationComplete","guideXosNativeAotFirstPerThreadRootGcScanRootsEntered","guideXosNativeAotFirstPerThreadRootForeachThreadEntered","guideXosNativeAotFirstPerThreadRootIteratorInitialized","guideXosNativeAotFirstPerThreadRootIteratorCompletion","guideXosNativeAotFirstPerThreadRootThreadEnumerated","guideXosNativeAotFirstPerThreadRootThreadExcluded","guideXosNativeAotFirstPerThreadRootThreadIncluded","guideXosNativeAotFirstPerThreadRootThreadStaticListObserved","guideXosNativeAotFirstPerThreadRootThreadStaticStorageEntered","guideXosNativeAotFirstNonNullRootCandidateLoadRequested","guideXosNativeAotFirstNonNullRootCandidateMachineWordLoaded","guideXosNativeAotFirstRootCallbackCallSiteEntered","guideXosNativeAotFirstRootCallbackEntered","guideXosNativeAotFirstRootCallbackCandidateLoaded","guideXosManagedThreadStaticProofAssigned","guideXosManagedThreadStaticProofReadback")
        if ($isFirstRootFirstNonNullOldO) {
            $requiredSymbols += @("guideXosNativeAotFirstRootCallbackCallSiteEntered","guideXosNativeAotFirstRootNonNullOldOCandidateLoadRequested","guideXosNativeAotFirstRootNonNullOldOCandidateMachineWordLoaded","guideXosNativeAotFirstRootNonNullOldOCandidateLoaded","guideXosNativeAotFirstRootNonNullOldOCallbackEntered","guideXosNativeAotFirstRootNonNullOldOMarkHelperEntered","guideXosNativeAotFirstRootNonNullOldOWorklistWriteBefore","guideXosNativeAotFirstRootNonNullOldOWorklistWriteCompleted","guideXosNativeAotFirstRootNonNullOldODecisionRequested","guideXosNativeAotFirstRootNonNullOldODecisionEntered","guideXosNativeAotFirstRootNonNullOldONullDecisionCompleted","guideXosNativeAotFirstRootNonNullOldONonNullDecisionStarted","guideXosNativeAotFirstRootNonNullOldOMarkedRequested","guideXosNativeAotFirstRootNonNullOldOMarkedCompleted","guideXosNativeAotFirstRootNonNullOldORawMarkWordRead","guideXosNativeAotFirstRootNonNullOldOMarkedTrueBoundary","guideXosNativeAotFirstRootNonNullOldOMarkedFalseBoundary")
        }
    } elseif ($isFirstNonNullRoot) {
        $requiredSymbols += @("guideXosNativeAotAllocationContextFixupRequest","guideXosNativeAotAllocationContextFixupGcStartWorkObserver","guideXosNativeAotAllocationRootPhaseRequested","guideXosNativeAotAllocationContextFixupEnumerationEntry","guideXosNativeAotAllocationContextFixupContextVisited","guideXosNativeAotAllocationContextFixupEnumerationComplete","guideXosNativeAotFirstPerThreadRootGcScanRootsEntered","guideXosNativeAotFirstPerThreadRootForeachThreadEntered","guideXosNativeAotFirstPerThreadRootIteratorInitialized","guideXosNativeAotFirstPerThreadRootIteratorCompletion","guideXosNativeAotFirstPerThreadRootThreadEnumerated","guideXosNativeAotFirstPerThreadRootThreadExcluded","guideXosNativeAotFirstPerThreadRootThreadIncluded","guideXosNativeAotFirstPerThreadRootThreadStaticListObserved","guideXosNativeAotFirstPerThreadRootThreadStaticStorageEntered","guideXosNativeAotFirstNonNullRootCandidateLoadRequested","guideXosNativeAotFirstNonNullRootCandidateMachineWordLoaded","guideXosManagedThreadStaticProofAssigned","guideXosManagedThreadStaticProofReadback")
    } elseif ($isFirstRootCandidateLoad) {
        $requiredSymbols += @("guideXosNativeAotAllocationContextFixupRequest","guideXosNativeAotAllocationContextFixupGcStartWorkObserver","guideXosNativeAotAllocationRootPhaseRequested","guideXosNativeAotAllocationContextFixupEnumerationEntry","guideXosNativeAotAllocationContextFixupContextVisited","guideXosNativeAotAllocationContextFixupEnumerationComplete","guideXosNativeAotFirstPerThreadRootGcScanRootsEntered","guideXosNativeAotFirstPerThreadRootForeachThreadEntered","guideXosNativeAotFirstPerThreadRootIteratorInitialized","guideXosNativeAotFirstPerThreadRootIteratorCompletion","guideXosNativeAotFirstPerThreadRootThreadEnumerated","guideXosNativeAotFirstPerThreadRootThreadExcluded","guideXosNativeAotFirstPerThreadRootThreadIncluded","guideXosNativeAotFirstPerThreadRootThreadStaticListObserved","guideXosNativeAotFirstPerThreadRootThreadStaticStorageEntered","guideXosNativeAotFirstRootCandidateLoadRequested","guideXosNativeAotFirstRootCandidateMachineWordLoaded")
    } elseif ($isFirstPerThreadRootProvider) {
        $requiredSymbols += @("guideXosNativeAotAllocationContextFixupRequest","guideXosNativeAotAllocationContextFixupGcStartWorkObserver","guideXosNativeAotAllocationRootPhaseRequested","guideXosNativeAotAllocationContextFixupEnumerationEntry","guideXosNativeAotAllocationContextFixupContextVisited","guideXosNativeAotAllocationContextFixupEnumerationComplete","guideXosNativeAotFirstPerThreadRootGcScanRootsEntered","guideXosNativeAotFirstPerThreadRootForeachThreadEntered","guideXosNativeAotFirstPerThreadRootIteratorInitialized","guideXosNativeAotFirstPerThreadRootIteratorCompletion","guideXosNativeAotFirstPerThreadRootThreadEnumerated","guideXosNativeAotFirstPerThreadRootThreadExcluded","guideXosNativeAotFirstPerThreadRootThreadIncluded","guideXosNativeAotFirstPerThreadRootThreadStaticListObserved","guideXosNativeAotFirstPerThreadRootThreadStaticStorageEntered")
    } elseif ($isAllocationContextFixupRootBoundary) {
        $requiredSymbols += @("guideXosNativeAotAllocationContextFixupRequest","guideXosNativeAotAllocationContextFixupGcStartWorkObserver","guideXosNativeAotAllocationRootPhaseRequested","guideXosNativeAotAllocationContextFixupRootBoundary","guideXosNativeAotAllocationContextFixupEnumerationEntry","guideXosNativeAotAllocationContextFixupContextVisited","guideXosNativeAotAllocationContextFixupEnumerationComplete")
    } else {
        $requiredSymbols += "guideXosNativeAotSuspendEeGcStartWorkBoundary"
    }
    foreach ($symbol in $requiredSymbols) { [void](Extract-MapAddress $mapText $symbol) }
    Assert-Text $mapText 'GcAllocInternal' "decorated GcAllocInternal map symbol"

    $exports = [ordered]@{
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_INSTALL_PAL_ADDRESS = "GuideXosNativeAotGcStartupInstallPalHooks"
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_INSTALL_TABLE_ADDRESS = "GuideXosNativeAotGcStartupInstallHookTable"
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_INSTALL_PLATFORM_ADDRESS = "GuideXosNativeAotGcStartupInstallPlatformHooks"
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_STARTUP_MAIN_ADDRESS = "GuideXosNativeAotGcStartupMain"
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_GET_STATE_ADDRESS = "GuideXosNativeAotGcStartupGetState"
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_GET_PRE_GC_STATE_ADDRESS = "GuideXosNativeAotGcStartupGetPreGcState"
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_GET_ALLOCATION_COUNT_ADDRESS = "GuideXosNativeAotGcStartupGetAllocationCount"
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_GET_LAST_ALLOCATION_SIZE_ADDRESS = "GuideXosNativeAotGcStartupGetLastAllocationSize"
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_GET_DIAGNOSTIC_STAGE_ADDRESS = "GuideXosNativeAotGcStartupGetDiagnosticStage"
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_MANAGED_MAIN_ADDRESS = "ManagedMain"
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_BEGIN_EXPERIMENT_ADDRESS = "guideXosManagedAllocationBeginFirstCollectionBoundaryExperiment"
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_FINALIZE_ADDRESS = "guideXosManagedAllocationFinalize"
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_GET_DIAGNOSTICS_ADDRESS = "guideXosManagedAllocationGetDiagnostics"
    }
    if ($isC011EC23) {
        $exports.GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_STANDALONE_NATIVE_UNWIND_ADDRESS = "guideXosNativeAotC011EC23StandaloneNativeUnwind"
    }
    $headerLines = @("#pragma once", "", "#include <stdint.h>", "")
    foreach ($define in $exports.Keys) { $headerLines += "#define $define ((uintptr_t)0x$(Extract-MapAddress $mapText $exports[$define])u)" }
    $exportHeader = Join-Path $artifactRoot "guidexos_nativeaot_gc_single_thread_suspend_ee_exports.h"
    Set-Content -LiteralPath $exportHeader -Value $headerLines -Encoding ASCII
    Copy-Item -LiteralPath $exportHeader -Destination (Join-Path $runRoot "guidexos_nativeaot_gc_single_thread_suspend_ee_exports.h") -Force
    $rawObj = Join-Path $buildRoot "gc-single-thread-suspend-ee-artifact.raw.o"
    $embeddedObj = Join-Path $buildRoot "gc-single-thread-suspend-ee-artifact.o"
    Invoke-LoggedCommand $objcopy @("-I","binary","-O","pe-x86-64","-B","i386:x86-64",$elfPath,$rawObj) (Join-Path $runRoot "embed-raw.log")
    $symbols = & $objdump -t $rawObj
    $startSymbol = ($symbols | Where-Object { $_ -match '(_binary_\S+_start)\s*$' } | ForEach-Object { $Matches[1] } | Select-Object -First 1)
    $endSymbol = ($symbols | Where-Object { $_ -match '(_binary_\S+_end)\s*$' } | ForEach-Object { $Matches[1] } | Select-Object -First 1)
    $sizeSymbol = ($symbols | Where-Object { $_ -match '(_binary_\S+_size)\s*$' } | ForEach-Object { $Matches[1] } | Select-Object -First 1)
    if ([string]::IsNullOrWhiteSpace($startSymbol) -or [string]::IsNullOrWhiteSpace($endSymbol) -or [string]::IsNullOrWhiteSpace($sizeSymbol)) { throw "Embedded symbol extraction failed." }
    Copy-Item -LiteralPath $rawObj -Destination $embeddedObj -Force
    Invoke-LoggedCommand $objcopy @("--redefine-sym","${startSymbol}=guidexos_nativeaot_gc_startup_artifact_start","--redefine-sym","${endSymbol}=guidexos_nativeaot_gc_startup_artifact_end","--redefine-sym","${sizeSymbol}=guidexos_nativeaot_gc_startup_artifact_size","--set-section-alignment",".data=4096","--rename-section",".data=.data,alloc,load,readonly,data,contents",$embeddedObj) (Join-Path $runRoot "embed-final.log")

    $c21KernelDefine = if ($isC011EC21) { " -DGUIDEXOS_NATIVEAOT_C011EC21_NATIVE_CONTINUATION" } else { "" }
    $c23KernelDefine = if ($isC011EC23) { " -DGUIDEXOS_NATIVEAOT_C011EC23_NATIVE_UNWIND" } else { "" }
    $c24KernelDefine = if ($isC011EC24) { " -DGUIDEXOS_NATIVEAOT_C011EC24_CALLER_PROVENANCE" } else { "" }
    $c25KernelDefine = if ($isC011EC25) { " -DGUIDEXOS_NATIVEAOT_C011EC25_KERNEL_ENTRY_BOUNDARY" } else { "" }
    $c26KernelDefine = if ($isC011EC26) { " -DGUIDEXOS_NATIVEAOT_C011EC26_STACK_COMPLETION" } else { "" }
    $c27KernelDefine = if ($isC011EC27) { " -DGUIDEXOS_NATIVEAOT_C011EC27_POST_ROOT_QUEUE" } else { "" }
    $extraCflags = "-DGXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST -DGXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_QEMU_TEST$c21KernelDefine$c23KernelDefine$c24KernelDefine$c25KernelDefine$c26KernelDefine$c27KernelDefine -I$artifactRoot"
    Set-Content -LiteralPath (Join-Path $runRoot "selectors.txt") -Value @("GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST=1","GXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_QEMU_TEST=1","C011EC21_NATIVE_CONTINUATION=$isC011EC21","C011EC23_NATIVE_UNWIND=$isC011EC23","C011EC24_CALLER_PROVENANCE=$isC011EC24","C011EC25_KERNEL_ENTRY_BOUNDARY=$isC011EC25","C011EC26_STACK_COMPLETION=$isC011EC26","C011EC27_POST_ROOT_QUEUE=$isC011EC27","NATIVEAOT_GC_STARTUP_QEMU_ARTIFACT_OBJ=$embeddedObj") -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $runRoot "extra-cflags.txt") -Value $extraCflags -Encoding ASCII
    Invoke-LoggedCommand $make @("-C","kernel","ARCH=amd64","clean") (Join-Path $runRoot "kernel-preclean.log")
    Invoke-LoggedCommand $make @("-C","kernel","ARCH=amd64","GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST=1","GXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_QEMU_TEST=1","NATIVEAOT_GC_STARTUP_QEMU_ARTIFACT_OBJ=$embeddedObj","EXTRA_CFLAGS=$extraCflags") (Join-Path $runRoot "kernel-build.log")
    Require-File $kernelPath "Specialized single-thread SuspendEE kernel"
    $specializedKernelHash = Hash-File $kernelPath
    Set-Content -LiteralPath (Join-Path $runRoot "kernel-symbols.txt") -Value (& $objdump -t $kernelPath) -Encoding ASCII
    $nativeHelperAudit = $null
    $nativeEntryAudit = $null
    if ($isC011EC25) {
        $kernelEntrySymbolsText = (& $objdump -t -C $kernelPath 2>&1) -join "`n"
        $kernelEntryDisassemblyText = (& $objdump -d -C -j .boot $kernelPath 2>&1) -join "`n"
        $kernelEntrySectionsText = (& $objdump -h $kernelPath 2>&1) -join "`n"
        $kernelEntryUnwindText = (& $objdump -Wf $kernelPath 2>&1) -join "`n"
        $startMatch = [regex]::Match(
            $kernelEntrySymbolsText,
            '(?im)^\s*(?<address>[0-9a-f]{16})\s+\w\s+\.boot\s+\S*\s+_start$')
        $haltMatch = [regex]::Match(
            $kernelEntrySymbolsText,
            '(?im)^\s*(?<address>[0-9a-f]{16})\s+\w\s+\.boot\s+\S*\s+_start\.halt$')
        $kernelMainMatch = [regex]::Match(
            $kernelEntrySymbolsText,
            '(?im)^\s*(?<address>[0-9a-f]{16})\s+\w\s+\w\s+\.text\s+\S*\s+kernel_main(?:\(.*\))?$')
        $stackTopMatch = [regex]::Match(
            $kernelEntrySymbolsText,
            '(?im)^\s*(?<address>[0-9a-f]{16})\s+\w\s+\.bss\s+\S*\s+boot_stack_top$')
        if (-not $startMatch.Success -or -not $haltMatch.Success -or
            -not $kernelMainMatch.Success -or -not $stackTopMatch.Success) {
            throw "C011EC25 static entry audit could not resolve _start, _start.halt, kernel_main, and boot_stack_top."
        }
        $entryAddress = [Convert]::ToUInt64($startMatch.Groups['address'].Value, 16)
        $haltAddress = [Convert]::ToUInt64($haltMatch.Groups['address'].Value, 16)
        $kernelMainAddress = [Convert]::ToUInt64($kernelMainMatch.Groups['address'].Value, 16)
        $bootStackTopAddress = [Convert]::ToUInt64($stackTopMatch.Groups['address'].Value, 16)
        $haltHex = $haltAddress.ToString('x')
        $haltLoopHex = ($haltAddress + 1).ToString('x')
        $callMatch = [regex]::Match(
            $kernelEntryDisassemblyText,
            '(?im)^\s*[0-9a-f]+:\s+e8\s+[0-9a-f ]+\s+call\s+(?<target>[0-9a-f]+)\s+<kernel_main>')
        if (-not $callMatch.Success -or
            [Convert]::ToUInt64($callMatch.Groups['target'].Value, 16) -ne $kernelMainAddress) {
            throw "C011EC25 static entry audit did not find a direct call into kernel_main."
        }
        if ($kernelEntryDisassemblyText -notmatch ("(?im)^\s*{0}:\s+f4\s+hlt\s*$" -f [regex]::Escape($haltHex)) -or
            $kernelEntryDisassemblyText -notmatch ("(?im)^\s*{0}:\s+eb\s+fd\s+jmp\s+{1}\b" -f [regex]::Escape($haltLoopHex), [regex]::Escape($haltHex))) {
            throw "C011EC25 static entry audit did not find the _start.halt hlt/jmp loop."
        }
        if ($kernelEntrySectionsText -match '(?im)^\s*\d+\s+\.eh_frame\s' -or
            $kernelEntryUnwindText -match '(?im)FDE\s+cie') {
            throw "C011EC25 static entry audit found compiler unwind metadata for the .boot entry boundary."
        }
        Set-Content -LiteralPath (Join-Path $runRoot 'kernel-entry-symbols.txt') -Value $kernelEntrySymbolsText -Encoding ASCII
        Set-Content -LiteralPath (Join-Path $runRoot 'kernel-entry-disassembly.txt') -Value $kernelEntryDisassemblyText -Encoding ASCII
        Set-Content -LiteralPath (Join-Path $runRoot 'kernel-entry-sections.txt') -Value $kernelEntrySectionsText -Encoding ASCII
        Set-Content -LiteralPath (Join-Path $runRoot 'kernel-entry-unwind-audit.txt') -Value $kernelEntryUnwindText -Encoding ASCII
        $nativeEntryAudit = [ordered]@{
            linkedEntry=('0x' + $startMatch.Groups['address'].Value.ToUpperInvariant())
            linkedHalt=('0x' + $haltMatch.Groups['address'].Value.ToUpperInvariant())
            kernelMain=('0x' + $kernelMainMatch.Groups['address'].Value.ToUpperInvariant())
            bootStackTop=('0x' + $stackTopMatch.Groups['address'].Value.ToUpperInvariant())
            call='direct E8 call from _start .boot into kernel_main'
            halt='F4 hlt followed by EB FD self-loop at _start.halt'
            entryUnwind='no .eh_frame FDE; .pdata/.xdata are outside .boot'
            symbols=(Join-Path $runRoot 'kernel-entry-symbols.txt')
            disassembly=(Join-Path $runRoot 'kernel-entry-disassembly.txt')
            sections=(Join-Path $runRoot 'kernel-entry-sections.txt')
            unwindAudit=(Join-Path $runRoot 'kernel-entry-unwind-audit.txt')
        }
        if ($isC011EC26) {
            $terminalStartMatch = [regex]::Match(
                $kernelEntrySymbolsText,
                '(?im)^\s*(?<address>[0-9a-f]{16})\s+\w\s+\.boot\s+\S*\s+__guidexos_native_terminal_start$')
            $terminalEndMatch = [regex]::Match(
                $kernelEntrySymbolsText,
                '(?im)^\s*(?<address>[0-9a-f]{16})\s+\w\s+\.boot\s+\S*\s+__guidexos_native_terminal_end$')
            if (-not $terminalStartMatch.Success -or -not $terminalEndMatch.Success) {
                throw "C011EC26 static entry audit could not resolve the linker-exported native terminal range."
            }
            $terminalStartAddress = [Convert]::ToUInt64($terminalStartMatch.Groups['address'].Value, 16)
            $terminalEndAddress = [Convert]::ToUInt64($terminalEndMatch.Groups['address'].Value, 16)
            if ($terminalStartAddress -ne $haltAddress -or $terminalEndAddress -le $terminalStartAddress) {
                throw "C011EC26 linker-exported terminal range does not begin at _start.halt or has invalid extent."
            }
            $nativeEntryAudit.terminalStart=('0x' + $terminalStartMatch.Groups['address'].Value.ToUpperInvariant())
            $nativeEntryAudit.terminalEnd=('0x' + $terminalEndMatch.Groups['address'].Value.ToUpperInvariant())
        }
    }
    if ($isC011EC26) {
        $requiredSymbols += @("guideXosNativeAotC011EC26IteratorCompleted","guideXosNativeAotC011EC26StackProviderReturned","guideXosNativeAotC011EC26GcScanRootsEntered","guideXosNativeAotC011EC26GcScanRootsReturned","guideXosNativeAotC011EC26ThreadGcScanRootsEntered","guideXosNativeAotC011EC26ThreadGcScanRootsReturned","guideXosNativeAotC011EC26PostScanAfterGcScanRootsEntered")
    }
    if ($isC011EC27) {
        $requiredSymbols += @("guideXosNativeAotC011EC27PostRootAfterGcScanRootsEntered","guideXosNativeAotC011EC27QueueItemConsumed","guideXosNativeAotC011EC27MarkStateRead","guideXosNativeAotC011EC27MarkWriteAttempted","guideXosNativeAotC011EC27MarkWriteCompleted","guideXosNativeAotC011EC27ChildScanAttempted","guideXosNativeAotC011EC27ChildReferenceRead","guideXosNativeAotC011EC27ChildPromoteAttempted")
    }
    if ($isC011EC29) {
        $requiredSymbols += @("guideXosNativeAotC011EC29AfterGcScanRootsEntered","guideXosNativeAotC011EC29AfterGcScanRootsReturned","guideXosNativeAotC011EC29NextPhaseEntered","guideXosNativeAotC011EC29HandleScanEntered","guideXosNativeAotC011EC29HandleMapRead")
    }
    if ($isC011EC33) {
        $requiredSymbols += @("guideXosNativeAotC011EC33GcScanRootsEntered","guideXosNativeAotC011EC33GcScanRootsReturned","guideXosNativeAotC011EC33AfterGcScanRootsEntered","guideXosNativeAotC011EC33AfterGcScanRootsReturned","guideXosNativeAotC011EC33GcDoneEntered","guideXosNativeAotC011EC33RestartEEEntered","guideXosNativeAotC011EC33RestartEEReturned","guideXosNativeAotC011EC33LivenessCheckEntered","guideXosNativeAotC011EC33ClearingStoreEntered","guideXosNativeAotC011EC33LivenessDecisionObserved","guideXosNativeAotC011EC33WeakHandleAllocated","guideXosNativeAotC011EC33GetCompletedCollections","guideXosNativeAotC011EC33LifetimeBoundaryReturned")
    }
    if ($isC011EC34) {
        $requiredSymbols += @("guideXosNativeAotC011EC34RelocationPhaseEntered","guideXosNativeAotC011EC34GcScanRootsEntered","guideXosNativeAotC011EC34GcScanRootsReturned","guideXosNativeAotC011EC34GcRootReported","guideXosNativeAotC011EC34RelocateEntered","guideXosNativeAotC011EC34RelocateReturned","guideXosNativeAotC011EC34RelocationLookupEntered","guideXosNativeAotC011EC34RelocationLookupObserved","guideXosNativeAotC011EC34RelocationRootScanReturned")
    }
    if ($isC011EC30) {
        $requiredSymbols += @("guideXosNativeAotC011EC30HandleScanEntered","guideXosNativeAotC011EC30HandleMapRootRead","guideXosNativeAotC011EC30BucketVisited","guideXosNativeAotC011EC30HandleTableVisited","guideXosNativeAotC011EC30SegmentVisited","guideXosNativeAotC011EC30BlockVisited","guideXosNativeAotC011EC30HandleSlotInspected","guideXosNativeAotC011EC30HandleSlotCandidate","guideXosNativeAotC011EC30LivenessCheckEntered","guideXosNativeAotC011EC30LivenessDecisionObserved","guideXosNativeAotC011EC30LivenessDecisionCompleted","guideXosNativeAotC011EC30HandleScanCompleted")
    }
    if ($isC011EC31) {
        $requiredSymbols += @("guideXosNativeAotC011EC31HandleAllocationEntered","guideXosNativeAotC011EC31HandleAllocated","guideXosNativeAotC011EC31WeakHandleAllocated","guideXosNativeAotC011EC31StrongRootRecorded","guideXosNativeAotC011EC31StrongHandlePromoted","guideXosNativeAotC011EC31StrongRootCandidate","guideXosNativeAotC011EC31LivenessCheckEntered")
    }
    if ($isC011EC32) {
        $requiredSymbols += @("guideXosNativeAotC011EC32HandleAllocationEntered","guideXosNativeAotC011EC32HandleAllocated","guideXosNativeAotC011EC32WeakHandleAllocated","guideXosNativeAotC011EC32HelperReturned","guideXosNativeAotC011EC32StrongHandlePromoted","guideXosNativeAotC011EC32StrongRootCandidate","guideXosNativeAotC011EC32GcRootReported","guideXosNativeAotC011EC32LivenessCheckEntered","guideXosNativeAotC011EC32ClearingStoreEntered")
    }
    if ($isC011EC21) {
        $kernelSymbolsText = (& $objdump -t -C $kernelPath 2>&1) -join "`n"
        $nativeSectionsText = (& $objdump -h $kernelPath 2>&1) -join "`n"
        $nativeUnwindText = (& $objdump -Wf $kernelPath 2>&1) -join "`n"
        $helperSymbolMatch = [regex]::Match(
            $kernelSymbolsText,
            '(?im)^(?<address>[0-9a-f]{16})\s+\w\s+.*runFirstRealAllocationImpl')
        if (-not $helperSymbolMatch.Success) {
            throw "C011EC21 could not resolve runFirstRealAllocationImpl in the linked kernel symbols."
        }
        $helperAddress = [Convert]::ToUInt64($helperSymbolMatch.Groups['address'].Value, 16)
        $disassemblyStart = if ($helperAddress -gt 0x2000) { $helperAddress - 0x2000 } else { 0 }
        $disassemblyStop = $helperAddress + 0x2000
        $nativeDisassemblyText = (& $objdump -d -C ("--start-address=0x{0:X}" -f $disassemblyStart) ("--stop-address=0x{0:X}" -f $disassemblyStop) $kernelPath 2>&1) -join "`n"
        foreach ($kernelSymbol in @('guideXosNativeAotC011EC21DescribeNativeCaller')) {
            if ($kernelSymbolsText -notmatch [regex]::Escape($kernelSymbol)) {
                throw "C011EC21 kernel symbol audit could not resolve $kernelSymbol."
            }
        }
        if (-not $isC011EC24 -and $nativeDisassemblyText -notmatch '(?im)^\s*[0-9a-f]+:\s+ff 94 24 88 03 00 00\s+call') {
            throw "C011EC21 did not retain the source-correlated indirect ManagedMain call site in the native helper disassembly."
        }
        if ($nativeSectionsText -notmatch '(?im)\s\.text\s') {
            throw "C011EC21 kernel section audit did not find the executable .text section."
        }
        if ($nativeSectionsText -match '(?im)\s\.eh_frame\s' -or $nativeUnwindText -match '(?im)FDE\s+cie') {
            throw "C011EC21 expected no DWARF native unwind metadata for runFirstRealAllocationImpl, but an FDE was found."
        }
        Set-Content -LiteralPath (Join-Path $runRoot 'native-helper-symbols.txt') -Value $kernelSymbolsText -Encoding ASCII
        Set-Content -LiteralPath (Join-Path $runRoot 'native-helper-disassembly.txt') -Value $nativeDisassemblyText -Encoding ASCII
        Set-Content -LiteralPath (Join-Path $runRoot 'native-helper-sections.txt') -Value $nativeSectionsText -Encoding ASCII
        Set-Content -LiteralPath (Join-Path $runRoot 'native-helper-unwind-audit.txt') -Value $nativeUnwindText -Encoding ASCII
        $nativeHelperAudit = [ordered]@{
            symbol='kernel::nativeaot_pal_qemu_test::(anonymous namespace)::runFirstRealAllocationImpl'
            linkedAddress=('0x' + $helperSymbolMatch.Groups['address'].Value.ToUpperInvariant())
            source='kernel/core/nativeaot_pal_qemu_test.cpp:1915-2356; managed call at source line 2200'
            module='kernel.elf'
            section='.text'
            callSite='indirect call [rsp+0x388] immediately before recovered return RIP; objdump-correlated'
            runtimeFunction=if ($isC011EC23) { 'genuine retained .pdata entry; see C011EC23-PREFLIGHT' } else { 'none in kernel .pdata' }
            unwindInfo=if ($isC011EC23) { 'genuine retained .xdata UNWIND_INFO; see C011EC23-PREFLIGHT' } else { 'none; no .xdata/.eh_frame FDE covering helper' }
            nativeSymbols=(Join-Path $runRoot 'native-helper-symbols.txt')
            disassembly=(Join-Path $runRoot 'native-helper-disassembly.txt')
            sections=(Join-Path $runRoot 'native-helper-sections.txt')
            unwindAudit=(Join-Path $runRoot 'native-helper-unwind-audit.txt')
        }
    }

    if ($isC011EC30) { $runResults = @() }
    for ($runIndex = 0; $runIndex -lt $FreshBootCount; $runIndex++) {
        $name = if ($runIndex -eq 0) { "first-run" } else { "repeat-$runIndex" }
        $oneRoot = Join-Path $runRoot $name
        $espRoot = Join-Path $oneRoot "esp"
        $bootPath = Join-Path $espRoot "EFI\BOOT\BOOTX64.EFI"
        $serialPath = Join-Path $oneRoot "serial.log"
        New-Item -ItemType Directory -Force -Path (Split-Path $bootPath) | Out-Null
        Copy-Item -LiteralPath $bootloader -Destination $bootPath -Force
        Copy-Item -LiteralPath $kernelPath -Destination (Join-Path $espRoot "kernel.elf") -Force
        $port = 44800 + $runIndex
        $monitorPath = Join-Path $oneRoot "watchdog-monitor.txt"
        $qemuDebugPath = Join-Path $oneRoot "qemu-debug.log"
        $qemuArgs = @("-accel","tcg,thread=single","-machine","pc","-smp","1","-drive",('if=pflash,format=raw,readonly=on,file="' + $ovmf + '"'),"-drive",('file=fat:rw:"' + $espRoot + '",format=raw,if=ide,index=0'),"-m","1024M","-vga","std","-display","none","-serial",('file:"' + $serialPath + '"'),"-monitor",("tcp:127.0.0.1:$port,server,nowait"),"-no-reboot","-no-shutdown","-rtc","base=utc,clock=host")
        if ($isStackProviderTransitionFailFast -or $isCodeManagerRegistration -or $isTransitionFrameControlPc -or $isC011EC19 -or $isC011EC33 -or $isFirstNonNullRoot -or $isFirstRootCallbackEntry) { $qemuArgs += @("-d","int,guest_errors","-D",$qemuDebugPath) }
        Log-Command ('"' + $qemu + '" ' + ($qemuArgs -join ' '))
        $qemuProcess = Start-Process -FilePath $qemu -ArgumentList $qemuArgs -WindowStyle Hidden -PassThru
        $completed = $false
        $earlyFailure = $null
        try {
            $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
            while ((Get-Date) -lt $deadline -and -not $qemuProcess.HasExited) {
                Start-Sleep -Milliseconds 250
                if (Test-Path -LiteralPath $serialPath) {
                    $liveText = Get-Content -LiteralPath $serialPath -Raw
                    $normalizedLiveText = $liveText -replace '\[IRQ\] dispatch irq=00\s*', ''
                    $normalizedLiveText = ($normalizedLiveText -creplace '(?<=[0-9])(?=[a-z])', ' ') -replace '\s+', ' '
                    $normalizedLiveText = $normalizedLiveText -replace '\b(c\d+)\s+(ec\d+)', '$1$2'
                    $normalizedLiveText = $normalizedLiveText -replace '\s*=\s*', '='
                    $stopPattern = if ($isC011EC34) {
                        'marker=C011EC34(?:\s|$)|marker=C011EC34-BLOCKED'
                    } elseif ($isC011EC33) {
                        'marker=C011EC33(?:\s|$)|marker=C011EC33-BLOCKED'
                    } elseif ($isC011EC23) {
                        if ($isC011EC32) { 'marker=C011EC32(?=.*safeStopReason=)' } elseif ($isC011EC31) { 'marker=C011EC31(?=.*safeStopReason=)' } elseif ($isC011EC30) { 'marker=C011EC30(?=.*safeStopReason=)' } elseif ($isC011EC29) { 'marker=C011EC29(?=.*safeStopReason=)' } elseif ($isC011EC28) { 'marker=C011EC28(?=.*safeStopReason=)' } elseif ($isC011EC27Stop) { 'marker=C011EC27(?:\s|-)' } elseif ($isC011EC26) { 'marker=C011EC26(\s|$)' } elseif ($isC011EC25) { 'marker=C011EC25(\s|$)' } elseif ($isC011EC24) { 'marker=C011EC24(\s|$)' } else { 'marker=C011EC23(\s|$)' }
                    } elseif ($isC011EC21) {
                        'marker=C011EC21'
                    } elseif ($isC011EC20) {
                        'marker=C011EC20'
                    } elseif ($isC011EC19) {
                        'marker=C011EC19'
                    } elseif ($isCodeManagerRegistration) {
                        '\[nativeaot-pal-qemu-test\] FAIL_FAST reason=47435354|marker=C011EC15'
                    } elseif ($isStackProviderTransitionFailFast) {
                        '\[nativeaot-pal-qemu-test\] FAIL_FAST reason=47435354'
                    } elseif ($isNextGenuineRootProvider) {
                        'marker=C011EC15'
                    } elseif ($isFirstRootFirstNonNullOldO) {
                        'marker=C011EC14'
                    } elseif ($isFirstRootPostQueueMarkDecision) {
                        'marker=C011EC13'
                    } elseif ($isFirstRootFirstMarkMutation) {
                        'marker=C011EC12'
                    } elseif ($isFirstRootPreMarkBoundary) {
                        'marker=C011EC11'
                    } elseif ($isFirstRootCondemnedGenerationDecision) {
                        'marker=C011EC10'
                    } elseif ($isFirstRootHeapResolution) {
                        'marker=C011EC09'
                    } elseif ($isFirstRootMembershipClassification) {
                        'marker=C011EC08'
                    } elseif ($isFirstRootCallbackEntry) {
                        'marker=C011EC07'
                    } elseif ($isFirstNonNullRoot) {
                        'lockDepth=00000001 marker=C011EC06'
                    } elseif ($isFirstRootCandidateLoad) {
                        'lockDepth=00000001 marker=C011EC05'
                    } elseif ($isFirstPerThreadRootProvider) {
                        'lockDepth=00000001 marker=C011EC04'
                    } elseif ($isAllocationContextFixupRootBoundary) {
                        'segmentReservedAfter=0000000100B00000'
                    } else {
                        'liveSentinels=00000004'
                    }
                    if ($normalizedLiveText -match $stopPattern) { $completed = $true; break }
                    if ($isStackProviderTransitionFailFast -and
                        $normalizedLiveText -match '\[nativeaot-pal-qemu-test\] FAIL_FAST reason=47435354') {
                        $earlyFailure = "stack-provider-transition-failfast-runtime-prologue"
                        break
                    }
                    if ($isFirstRootFirstNonNullOldO -and
                        $normalizedLiveText -match '\[nativeaot-pal-qemu-test\] FAIL_FAST reason=') {
                        $earlyFailure = "c011ec14-no-non-null-old-o-before-nativeaot-failfast"
                        break
                    }
                    if (($isFirstNonNullRoot -or $isFirstRootCallbackEntry) -and $normalizedLiveText -match '\[PageFault\] Not-present violation on read \(kernel\)') { $earlyFailure = "nativeaot-thread-static-startup-page-fault"; break }
                }
            }
            if (-not $completed -and [string]::IsNullOrWhiteSpace($earlyFailure)) {
                Read-Monitor $port $monitorPath
                $failureSerial = if (Test-Path -LiteralPath $serialPath) { Get-Content -LiteralPath $serialPath -Raw } else { "" }
                if (($isFirstNonNullRoot -or $isFirstRootCallbackEntry) -and $failureSerial -match '\[PageFault\] Not-present violation on read \(kernel\)') {
                    $earlyFailure = "nativeaot-thread-static-startup-page-fault"
                } elseif ($isC011EC34 -and $failureSerial -match 'marker=C011EC33-LIVE') {
                    $earlyFailure = "relocation-root-scan-timeout-after-c011ec33-relocation-entry"
                } elseif ($isC011EC33 -and $failureSerial -match 'marker=C011EC33-LIVE') {
                    $earlyFailure = "collection1-post-weak-completion-timeout"
                } elseif ($qemuProcess.HasExited) {
                    throw "QEMU $name exited before the NativeAOT GC safe-stop marker."
                } else {
                    throw "QEMU $name timed out after $TimeoutSeconds seconds."
                }
            }
        } finally {
            if (-not $qemuProcess.HasExited) { Stop-Process -Id $qemuProcess.Id -Force }
            try { $qemuProcess.WaitForExit() } catch { }
            [ordered]@{ run=$name; timeoutSeconds=$TimeoutSeconds; safeStopObserved=$completed; earlyFailure=$earlyFailure; harnessTerminated=$true; processId=$qemuProcess.Id; serialPath=$serialPath; monitorPath=$monitorPath; qemuDebugPath=$qemuDebugPath } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $oneRoot "watchdog.json") -Encoding ASCII
        }
        Require-File $serialPath "Fresh QEMU serial log"
        $serial = Get-Content -LiteralPath $serialPath -Raw
        Set-Content -LiteralPath (Join-Path $oneRoot "serial.sha256") -Value (Hash-File $serialPath) -Encoding ASCII
        $postWeakPhaseLines = @($serial -split '\r?\n' | Where-Object { $_ -match 'POST-WEAK phase=' })
        $validationText = $serial -replace '\[IRQ\] dispatch irq=00\s*', ''
        $validationText = ($validationText -creplace '(?<=[0-9])(?=[a-z])', ' ') -replace '\s+', ' '
        $validationText = $validationText -replace '\b(c\d+)\s+(ec\d+)', '$1$2'
        $validationText = $validationText -replace '\s*=\s*', '='
        $validationText = $validationText -replace '\s*-\s*', '-'
        if ($isC011EC34) {
            $c34PreflightStart = $validationText.IndexOf('[nativeaot-gc-relocation-root-update] PREFLIGHT marker=C011EC34-PREFLIGHT')
            $c34CompleteStart = $validationText.IndexOf('[nativeaot-gc-relocation-root-update] COMPLETE marker=C011EC34')
            if ($c34CompleteStart -lt 0) {
                $c34EvidenceStart = if ($c34PreflightStart -ge 0) { $c34PreflightStart } else { $validationText.IndexOf('[nativeaot-gc-short-weak-lifetime] LIVE marker=C011EC33-LIVE') }
                $c34Marker = if ($c34EvidenceStart -ge 0) { $validationText.Substring($c34EvidenceStart) } else { $validationText.Substring([Math]::Max(0, $validationText.Length - 12000)) }
                $runResults += [ordered]@{ name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker=if($c34PreflightStart -ge 0){'C011EC34-TIMEOUT'}else{'C011EC34-NO-PREFLIGHT'}; outcome='D / relocation root scan did not return to GC WKS'; successLevel=0; harnessTerminated=$true; markerLine=$c34Marker; preflightMarkerLine=if($c34PreflightStart -ge 0){$validationText.Substring($c34PreflightStart)}else{$null}; earlyFailure=$earlyFailure; postWeakPhaseLines=$postWeakPhaseLines; serialTail=if($validationText.Length -gt 12000){$validationText.Substring($validationText.Length - 12000)}else{$validationText} }
                continue
            }
            if ($c34PreflightStart -lt 0 -or $c34PreflightStart -gt $c34CompleteStart) { throw "C011EC34 preflight/completion chronology was incomplete in $name." }
            $c34PreflightLine = $validationText.Substring($c34PreflightStart, $c34CompleteStart - $c34PreflightStart)
            $c34MarkerLine = $validationText.Substring($c34CompleteStart)
            $c34OutcomeMatch = [regex]::Match($c34MarkerLine, 'marker=C011EC34 outcome=(?<value>[ABDE])')
            if (-not $c34OutcomeMatch.Success) { throw "C011EC34 completion outcome was missing in $name." }
            $c34Outcome = $c34OutcomeMatch.Groups['value'].Value
            $c34Read = { param([string]$Line,[string]$Field) $v=Get-MarkerField $Line $Field; if($null -eq $v){throw "C011EC34 missing field $Field."}; [Convert]::ToUInt64($v.Substring(2),16) }
            if ($c34Outcome -eq 'D') {
                $runResults += [ordered]@{ name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker='C011EC34-D'; outcome='D / relocation root scan returned with an invariant failure'; successLevel=0; harnessTerminated=$true; markerLine=$c34MarkerLine; preflightMarkerLine=$c34PreflightLine; earlyFailure=$earlyFailure }
                continue
            }
            if ($c34Outcome -ne 'A' -and $c34Outcome -ne 'B') { throw "C011EC34 emitted an unknown completion outcome in $name." }
            foreach ($check in @(@('successLevel',1),@('relocating',1),@('promotion',0),@('gcScanRootsEntries',1),@('gcScanRootsReturns',1),@('eeSuspended',1),@('threadStoreLockHeld',1),@('managedEntryProhibited',1),@('safeStopReason',0))) {
                if ((& $c34Read $c34MarkerLine $check[0]) -ne [uint64]$check[1]) { throw "C011EC34 expected $($check[0])=$($check[1]) in $name." }
            }
            if ((& $c34Read $c34MarkerLine 'rootReports') -lt 1) { throw "C011EC34 observed no relocation root reports in $name." }
            if ((& $c34Read $c34MarkerLine 'callbackEntries') -lt 1 -or (& $c34Read $c34MarkerLine 'callbackEntries') -ne (& $c34Read $c34MarkerLine 'callbackReturns')) { throw "C011EC34 relocation callback entries/returns were not balanced in $name." }
            foreach ($field in @('scanContext','callback','firstRootSlot','oldRoot','newRoot','rootBefore','rootAfter','rootCallbackEntry','rootCallbackReturn','rootControlPC','rootGcInfo')) { if ((& $c34Read $c34MarkerLine $field) -eq 0) { throw "C011EC34 expected nonzero $field in $name." } }
            if ((& $c34Read $c34MarkerLine 'rootBefore') -ne (& $c34Read $c34MarkerLine 'oldRoot') -or (& $c34Read $c34MarkerLine 'rootAfter') -ne (& $c34Read $c34MarkerLine 'newRoot')) { throw "C011EC34 root slot before/after did not match the relocation values in $name." }
            $lookupEntries = & $c34Read $c34MarkerLine 'lookupEntries'
            $lookupReturns = & $c34Read $c34MarkerLine 'lookupReturns'
            if ($lookupEntries -ne $lookupReturns) { throw "C011EC34 relocation lookup entries/returns were unbalanced in $name." }
            if ($lookupEntries -ne 0 -and (& $c34Read $c34MarkerLine 'lookupAddress') -eq 0) { throw "C011EC34 relocation lookup metadata was absent in $name." }
            $rewritten = & $c34Read $c34MarkerLine 'rootRewritten'
            if ($c34Outcome -eq 'A' -and ($rewritten -ne 0 -or (& $c34Read $c34MarkerLine 'newRoot') -ne (& $c34Read $c34MarkerLine 'oldRoot'))) { throw "C011EC34 Outcome A was not an unchanged-root return in $name." }
            if ($c34Outcome -eq 'B' -and ($rewritten -ne 1 -or (& $c34Read $c34MarkerLine 'newRoot') -eq (& $c34Read $c34MarkerLine 'oldRoot'))) { throw "C011EC34 Outcome B was not a rewritten-root return in $name." }
            $runResults += [ordered]@{ name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker='C011EC34'; outcome=if($c34Outcome -eq 'A'){'A / relocation root callback returned with unchanged slot'}else{'B / relocation root callback returned with rewritten slot'}; successLevel=1; harnessTerminated=$true; markerLine=$c34MarkerLine; preflightMarkerLine=$c34PreflightLine }
            continue
        }
        if ($isC011EC33) {
            $c33LiveStart = $validationText.IndexOf('[nativeaot-gc-short-weak-lifetime] LIVE marker=C011EC33-LIVE')
            $c33PreflightStart = $validationText.IndexOf('[nativeaot-gc-short-weak-lifetime] PREFLIGHT marker=C011EC33-PREFLIGHT')
            $c33CompleteStart = $validationText.IndexOf('[nativeaot-gc-short-weak-lifetime] COMPLETE marker=C011EC33')
            $c33BlockedStart = $validationText.IndexOf('[nativeaot-gc-short-weak-lifetime] BLOCKED')
            if ($c33CompleteStart -lt 0) {
                if ($c33BlockedStart -ge 0) {
                    $runResults += [ordered]@{ name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker='C011EC33-BLOCKED'; outcome='D / Collection 1 or Collection 2 completion blocker'; successLevel=0; harnessTerminated=$true; markerLine=$validationText.Substring($c33BlockedStart); liveMarkerLine=if($c33LiveStart -ge 0){$validationText.Substring($c33LiveStart)}else{$null}; preflightMarkerLine=if($c33PreflightStart -ge 0){$validationText.Substring($c33PreflightStart)}else{$null}; earlyFailure=$earlyFailure }
                    continue
                }
                if ($c33LiveStart -ge 0 -and $earlyFailure -eq 'collection1-post-weak-completion-timeout') {
                    $runResults += [ordered]@{ name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker='C011EC33-LIVE'; outcome='D / Collection 1 post-weak GC completion blocked before EE restart'; successLevel=1; harnessTerminated=$true; markerLine=$validationText.Substring($c33LiveStart); liveMarkerLine=$validationText.Substring($c33LiveStart); preflightMarkerLine=if($c33PreflightStart -ge 0){$validationText.Substring($c33PreflightStart)}else{$null}; earlyFailure=$earlyFailure; postWeakPhaseLines=$postWeakPhaseLines; postWeakSerialTail=if($validationText.Length -gt 12000){$validationText.Substring($validationText.Length - 12000)}else{$validationText} }
                    continue
                }
                throw "C011EC33 completion evidence was incomplete in $name."
            }
            if ($c33LiveStart -lt 0 -or $c33PreflightStart -lt 0 -or $c33LiveStart -gt $c33PreflightStart -or $c33PreflightStart -gt $c33CompleteStart) { throw "C011EC33 live/preflight/completion chronology was incomplete in $name." }
            $c33LiveLine = $validationText.Substring($c33LiveStart, $c33PreflightStart - $c33LiveStart)
            $c33PreflightLine = $validationText.Substring($c33PreflightStart, $c33CompleteStart - $c33PreflightStart)
            $c33MarkerLine = $validationText.Substring($c33CompleteStart)
            $c33Read = { param([string]$Line,[string]$Field) $v=Get-MarkerField $Line $Field; if($null -eq $v){throw "C011EC33 missing field $Field."}; [Convert]::ToUInt64($v.Substring(2),16) }
            foreach ($check in @(@('successLevel',3),@('collection1TargetRoots',1),@('collection1TargetPromote',1),@('collection1TargetMarked',1),@('collection1Live',1),@('collection1Preserved',1),@('collection1Completed',1),@('collection2TargetRoots',0),@('collection2StackRoots',0),@('collection2RegisterRoots',0),@('collection2StaticThreadStaticRoots',0),@('collection2StrongHandles',0),@('collection2GraphPromotions',0),@('collection2QueueInsertions',0),@('collection2MarkWrites',0),@('collection2TargetMarked',0),@('collection2Dead',1),@('collection2Cleared',1),@('c1GcScanRootsEntries',1),@('c1GcScanRootsReturns',1),@('c2GcScanRootsEntries',1),@('c2GcScanRootsReturns',1),@('c1HandleScanEntries',1),@('c2HandleScanEntries',1),@('c1LivenessCallbacks',1),@('c2LivenessCallbacks',1),@('c1WeakSlotMatched',1),@('c2WeakSlotMatched',1),@('restartEntries',2),@('restartReturns',2),@('managedResume',2),@('safeStopReason',0))) {
                if ((& $c33Read $c33MarkerLine $check[0]) -ne [uint64]$check[1]) { throw "C011EC33 expected $($check[0])=$($check[1]) in $name." }
            }
            foreach ($field in @('initialTarget','targetAfterCollection1','weakSlot','collection1SlotBefore','collection1SlotAfter','collection2SlotBefore','collection2ClearingStore','collection1ControlPC','collection1MethodInfo','collection1MethodStart','collection1MethodEnd','collection1GcInfo','collection1SafePoint')) { if ((& $c33Read $c33MarkerLine $field) -eq 0) { throw "C011EC33 expected nonzero $field in $name." } }
            foreach ($pair in @(@('initialTarget','collection1SlotBefore'),@('collection1SlotBefore','collection1SlotAfter'),@('targetAfterCollection1','collection2SlotBefore'))) { if ((& $c33Read $c33MarkerLine $pair[0]) -ne (& $c33Read $c33MarkerLine $pair[1])) { throw "C011EC33 $($pair[0]) did not equal $($pair[1]) in $name." } }
            if ((& $c33Read $c33MarkerLine 'collection2SlotAfter') -ne 0) { throw "C011EC33 collection 2 weak slot was not cleared in $name." }
            $runResults += [ordered]@{ name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker='C011EC33'; outcome='C / same short-weak handle preserved in Collection 1 and cleared in Collection 2'; successLevel=3; harnessTerminated=$true; markerLine=$c33MarkerLine; liveMarkerLine=$c33LiveLine; preflightMarkerLine=$c33PreflightLine }
            continue
        }
        if ($isC011EC32) {
            $c32PreflightStart = $validationText.IndexOf('[nativeaot-gc-short-weak-dead] preflight marker=C011EC32-PREFLIGHT')
            $c32CompleteStart = $validationText.IndexOf('[nativeaot-gc-short-weak-dead] COMPLETE marker=C011EC32')
            $c32BlockedStart = $validationText.IndexOf('[nativeaot-gc-short-weak-dead] BLOCKED')
            $c29PreflightStart = $validationText.IndexOf('[nativeaot-gc-post-mark-phase] preflight marker=C011EC29-PREFLIGHT')
            if ($c32CompleteStart -lt 0) {
                if ($c32BlockedStart -ge 0) {
                    $runResults += [ordered]@{ name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker='C011EC32-BLOCKED'; outcome='H / C011EC32 dead short-weak proof blocker'; successLevel=0; harnessTerminated=$true; markerLine=$validationText.Substring($c32BlockedStart) }
                    continue
                }
                throw "C011EC32 completion evidence was incomplete in $name."
            }
            if ($c32PreflightStart -lt 0 -or $c32PreflightStart -gt $c32CompleteStart -or $c29PreflightStart -lt 0 -or $c29PreflightStart -gt $c32PreflightStart) { throw "C011EC32 predecessor/preflight chronology was incomplete in $name." }
            $c32PreflightLine = $validationText.Substring($c32PreflightStart, $c32CompleteStart - $c32PreflightStart)
            $c32MarkerLine = $validationText.Substring($c32CompleteStart)
            $c32Read = { param([string]$Line,[string]$Field) $v=Get-MarkerField $Line $Field; if($null -eq $v){throw "C011EC32 missing field $Field."}; [Convert]::ToUInt64($v.Substring(2),16) }
            foreach ($check in @(@('successLevel',3),@('c011ec32Preflight',1),@('c011ec29Preflight',1),@('weakHandleAllocationResult',1),@('weakHandleAllocationCallbacks',1),@('handleType',0),@('helperReturned',1),@('strongRootMatches',0),@('stackRootMatches',0),@('registerRootMatches',0),@('ordinaryRootMatches',0),@('staticThreadStaticRootMatches',0),@('threadAbortRootMatches',0),@('strongHandleMatches',0),@('graphDerivedPromotions',0),@('targetQueueInsertions',0),@('targetChildDiscoveries',0),@('targetMarkWrites',0),@('proofHandleMatched',1),@('bucketsVisited',1),@('tablesVisited',1),@('segmentsVisited',1),@('blocksVisited',1),@('shortWeakSlots',1),@('nonNullShortWeakHandles',1),@('livenessCallbacks',1),@('livenessDecisions',1),@('liveDecisions',0),@('deadDecisions',1),@('markedPromoted',0),@('livenessResult',0),@('mutationAttempted',1),@('clearingStore',1),@('preservedCount',0),@('clearedCount',1),@('queuePendingWork',0),@('markPendingWork',0),@('unexpectedWeakRooting',0),@('sensitiveAllocations',0),@('eeSuspended',1),@('threadStoreLockHeld',1),@('managedEntryProhibited',1),@('restart',0),@('resume',0),@('safeStopReason',0))) {
                if ((& $c32Read $c32MarkerLine $check[0]) -ne [uint64]$check[1]) { throw "C011EC32 expected $($check[0])=$($check[1]) in $name." }
            }
            if ((& $c32Read $c32MarkerLine 'allocationCount') -lt 1 -or (& $c32Read $c32MarkerLine 'allocationEntryCount') -lt 1) { throw "C011EC32 observed no production handle allocation in $name." }
            if ((& $c32Read $c32MarkerLine 'slotsInspected') -lt 1) { throw "C011EC32 observed no handle slots inspected in $name." }
            foreach ($field in @('allocationEntryAddress','helperReturnAddress','target','targetType','weakHandleSlot','weakHandleValueBefore','table','segment','block','blockFirstSlot','slot','markWordAddress','livenessCallbackFunction','livenessCallbackEntry','clearingStoreAddress')) { if ((& $c32Read $c32MarkerLine $field) -eq 0) { throw "C011EC32 expected nonzero $field in $name." } }
            if ((& $c32Read $c32MarkerLine 'bucketIndex') -eq [uint64]4294967295) { throw "C011EC32 did not capture the traversed bucket index in $name." }
            $target = & $c32Read $c32MarkerLine 'target'
            foreach ($pair in @(@('weakHandleValueBefore','target'),@('slotBefore','target'),@('slot','weakHandleSlot'),@('markWordAddress','target'))) {
                if ((& $c32Read $c32MarkerLine $pair[0]) -ne (& $c32Read $c32MarkerLine $pair[1])) { throw "C011EC32 $($pair[0]) did not equal $($pair[1]) in $name." }
            }
            if ((& $c32Read $c32MarkerLine 'slotAfter') -ne 0 -or (& $c32Read $c32MarkerLine 'blockType') -eq 0xFFFFFFFF -or (& $c32Read $c32MarkerLine 'slotIndex') -ge 64) { throw "C011EC32 clearing or block/slot metadata was invalid in $name." }
            $runResults += [ordered]@{ name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker='C011EC32'; outcome='C / dead short-weak handle cleared by production processing'; successLevel=3; harnessTerminated=$true; markerLine=$c32MarkerLine; preflightLine=$c32PreflightLine }
            continue
        }
        if ($isC011EC31) {
            $c31PreflightStart = $validationText.IndexOf('[nativeaot-gc-short-weak-live] preflight marker=C011EC31-PREFLIGHT')
            $c31CompleteStart = $validationText.IndexOf('[nativeaot-gc-short-weak-live] COMPLETE marker=C011EC31')
            $c31BlockedStart = $validationText.IndexOf('[nativeaot-gc-short-weak-live] BLOCKED')
            $c29PreflightStart = $validationText.IndexOf('[nativeaot-gc-post-mark-phase] preflight marker=C011EC29-PREFLIGHT')
            if ($c31CompleteStart -lt 0) {
                if ($c31BlockedStart -ge 0) {
                    $runResults += [ordered]@{ name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker='C011EC31-BLOCKED'; outcome='H / C011EC31 live short-weak proof blocker'; successLevel=0; harnessTerminated=$true; markerLine=$validationText.Substring($c31BlockedStart) }
                    continue
                }
                throw "C011EC31 completion evidence was incomplete in $name."
            }
            if ($c31PreflightStart -lt 0 -or $c31PreflightStart -gt $c31CompleteStart -or $c29PreflightStart -lt 0 -or $c29PreflightStart -gt $c31PreflightStart) { throw "C011EC31 predecessor/preflight chronology was incomplete in $name." }
            $c31PreflightLine = $validationText.Substring($c31PreflightStart, $c31CompleteStart - $c31PreflightStart)
            $c31MarkerLine = $validationText.Substring($c31CompleteStart)
            $c31Read = { param([string]$Line,[string]$Field) $v=Get-MarkerField $Line $Field; if($null -eq $v){throw "C011EC31 missing field $Field."}; [Convert]::ToUInt64($v.Substring(2),16) }
            foreach ($check in @(@('successLevel',3),@('c011ec31Preflight',1),@('c011ec29Preflight',1),@('weakHandleAllocationResult',1),@('weakHandleAllocationCallbacks',1),@('handleType',0),@('strongRootMatched',1),@('proofHandleMatched',1),@('bucketsVisited',1),@('tablesVisited',1),@('segmentsVisited',1),@('blocksVisited',1),@('shortWeakSlots',1),@('nonNullShortWeakHandles',1),@('livenessCallbacks',1),@('livenessDecisions',1),@('liveDecisions',1),@('deadDecisions',0),@('markedPromoted',1),@('livenessResult',1),@('mutationAttempted',0),@('preservedCount',1),@('clearedCount',0),@('queuePendingWork',0),@('markPendingWork',0),@('unexpectedWeakRooting',0),@('sensitiveAllocations',0),@('eeSuspended',1),@('threadStoreLockHeld',1),@('managedEntryProhibited',1),@('restart',0),@('resume',0),@('safeStopReason',0))) {
                if ((& $c31Read $c31MarkerLine $check[0]) -ne [uint64]$check[1]) { throw "C011EC31 expected $($check[0])=$($check[1]) in $name." }
            }
            if ((& $c31Read $c31MarkerLine 'allocationCount') -lt 1) { throw "C011EC31 observed no completed production handle allocation in $name." }
            if ((& $c31Read $c31MarkerLine 'allocationEntryCount') -lt 1) { throw "C011EC31 observed no production handle-allocation entry in $name." }
            if ((& $c31Read $c31MarkerLine 'strongHandlePromotions') -lt 1) { throw "C011EC31 observed no production strong-handle promotion in $name." }
            if ((& $c31Read $c31MarkerLine 'slotsInspected') -lt 1) { throw "C011EC31 observed no handle slots inspected in $name." }
            foreach ($field in @('allocationEntryAddress','target','strongRootSlot','strongRootValueBefore','weakHandleSlot','weakHandleValueBefore','table','segment','block','blockFirstSlot','slot','markWordAddress','livenessCallbackFunction','livenessCallbackEntry')) { if ((& $c31Read $c31MarkerLine $field) -eq 0) { throw "C011EC31 expected nonzero $field in $name." } }
            $target = & $c31Read $c31MarkerLine 'target'
            foreach ($pair in @(@('strongRootValueBefore','target'),@('weakHandleValueBefore','target'),@('slotBefore','target'),@('slotAfter','target'),@('slot','weakHandleSlot'))) {
                if ((& $c31Read $c31MarkerLine $pair[0]) -ne (& $c31Read $c31MarkerLine $pair[1])) { throw "C011EC31 $($pair[0]) did not equal $($pair[1]) in $name." }
            }
            if ((& $c31Read $c31MarkerLine 'blockType') -eq 0xFFFFFFFF -or (& $c31Read $c31MarkerLine 'slotIndex') -ge 64) { throw "C011EC31 proof block/slot metadata was invalid in $name." }
            $runResults += [ordered]@{ name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker='C011EC31'; outcome='C / live short-weak handle preserved by production processing'; successLevel=3; harnessTerminated=$true; markerLine=$c31MarkerLine; preflightLine=$c31PreflightLine }
            continue
        }
        if ($isC011EC30) {
            $c30PreflightStart = $validationText.IndexOf('[nativeaot-gc-short-weak-operation] preflight marker=C011EC30-PREFLIGHT')
            $c30CompleteStart = $validationText.IndexOf('[nativeaot-gc-short-weak-operation] COMPLETE marker=C011EC30')
            $c30BlockedStart = $validationText.IndexOf('[nativeaot-gc-short-weak-operation] BLOCKED')
            $c29PreflightStart = $validationText.IndexOf('[nativeaot-gc-post-mark-phase] preflight marker=C011EC29-PREFLIGHT')
            if ($c30CompleteStart -lt 0) {
                if ($c30BlockedStart -ge 0) {
                    $runResults += [ordered]@{ name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker='C011EC30-BLOCKED'; outcome='H / short-weak handle operation blocker'; successLevel=0; harnessTerminated=$true; markerLine=$validationText.Substring($c30BlockedStart) }
                    continue
                }
                throw "C011EC30 completion evidence was incomplete in $name."
            }
            if ($c30PreflightStart -lt 0 -or $c30PreflightStart -gt $c30CompleteStart -or $c29PreflightStart -lt 0 -or $c29PreflightStart -gt $c30PreflightStart) { throw "C011EC30 predecessor/preflight chronology was incomplete in $name." }
            $c30PreflightLine = $validationText.Substring($c30PreflightStart, $c30CompleteStart - $c30PreflightStart)
            $c30MarkerLine = $validationText.Substring($c30CompleteStart)
            $c30Read = { param([string]$Line,[string]$Field) $v=Get-MarkerField $Line $Field; if($null -eq $v){throw "C011EC30 missing field $Field."}; [Convert]::ToUInt64($v.Substring(2),16) }
            foreach ($check in @(@('successLevel',2),@('c011ec30Preflight',1),@('c011ec29Preflight',1),@('handleScanEntries',1),@('handleMapReads',1),@('bucketsVisited',1),@('tablesVisited',1),@('segmentsVisited',1),@('diagnosticMutationCount',0),@('safeStopReason',0))) {
                $value = & $c30Read $c30MarkerLine $check[0]
                if ($value -ne [uint64]$check[1] -and -not ($check[0] -eq 'successLevel' -and $value -eq 3)) { throw "C011EC30 expected $($check[0])=$($check[1]) in $name." }
            }
            $candidateHandles = & $c30Read $c30MarkerLine 'candidateHandles'
            $livenessDecisions = & $c30Read $c30MarkerLine 'livenessDecisions'
            $noHandleCompletion = & $c30Read $c30MarkerLine 'noHandleCompletion'
            if ($noHandleCompletion -eq 0) {
                foreach ($field in @('blocksVisited','slotsInspected')) { if ((& $c30Read $c30MarkerLine $field) -eq 0) { throw "C011EC30 expected nonzero $field for a liveness candidate in $name." } }
            } else {
                if ((& $c30Read $c30MarkerLine 'blocksVisited') -ne 0 -or (& $c30Read $c30MarkerLine 'slotsInspected') -ne 0) { throw "C011EC30 no-handle completion had unexpected block or slot observations in $name." }
            }
            if (($livenessDecisions -eq 1 -and ($candidateHandles -eq 0 -or $noHandleCompletion -ne 0)) -or ($noHandleCompletion -eq 1 -and ($candidateHandles -ne 0 -or $livenessDecisions -ne 0))) { throw "C011EC30 candidate/no-handle classification did not close in $name." }
            foreach ($field in @('mapAddress','bucketsFieldAddress','bucketsValue','maxIndex','firstBucketAddress','firstTableAddress','firstSegmentAddress')) { if ((& $c30Read $c30MarkerLine $field) -eq 0) { throw "C011EC30 expected nonzero $field in $name." } }
            if ($noHandleCompletion -eq 0 -and (& $c30Read $c30MarkerLine 'firstBlockAddress') -eq 0) { throw "C011EC30 expected nonzero firstBlockAddress for a liveness candidate in $name." }
            $runResults += [ordered]@{ name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker='C011EC30'; outcome='A / first authentic short-weak handle-table operation captured'; successLevel=(& $c30Read $c30MarkerLine 'successLevel'); harnessTerminated=$true; markerLine=$c30MarkerLine; preflightLine=$c30PreflightLine }
            continue
        }
        if ($isFirstNonNullRoot -and -not [string]::IsNullOrWhiteSpace($earlyFailure)) {
            Assert-Text $validationText '\[nativeaot-gc-single-thread-suspend-ee\] entering ManagedMain once' "managed entry before early failure"
            Assert-Text $validationText '\[PageFault\] Not-present violation on read \(kernel\)' "NativeAOT thread-static startup page fault"
            if ($validationText -match 'C011EC06|nativeaot-gc-first-non-null-root-callback-boundary|candidateVisited=|managedAssignmentCount=') { throw "The early-failure run reached proof-root or candidate-boundary evidence unexpectedly in $name." }
            $debugText = if (Test-Path -LiteralPath $qemuDebugPath -PathType Leaf) { Get-Content -LiteralPath $qemuDebugPath -Raw } else { "" }
            $faultRipMatches = [regex]::Matches($debugText, '(?im)\bRIP=(?<value>[0-9A-Fa-f]{16})')
            $faultCr2Matches = [regex]::Matches($debugText, '(?im)\bCR2=(?<value>[0-9A-Fa-f]{16})')
            $faultRip = if ($faultRipMatches.Count -gt 0) { "0x" + $faultRipMatches[$faultRipMatches.Count - 1].Groups['value'].Value.ToUpperInvariant() } else { $null }
            $faultCr2 = if ($faultCr2Matches.Count -gt 0) { "0x" + $faultCr2Matches[$faultCr2Matches.Count - 1].Groups['value'].Value.ToUpperInvariant() } else { $null }
            $qemuDebugSha256 = if (Test-Path -LiteralPath $qemuDebugPath -PathType Leaf) { Hash-File $qemuDebugPath } else { $null }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker=$null; outcome="E"; harnessTerminated=$true; earlyFailure=$earlyFailure
                managedMainEntered=$true; managedAssignmentCount="0x00000000"; managedReadbackCount="0x00000000"; candidateScanStarted=$false; callbackCount="0x00000000"; qemuDebugLog=$qemuDebugPath; qemuDebugSha256=$qemuDebugSha256; faultRip=$faultRip; faultCr2=$faultCr2
            }
            continue
        }
        if ($isC011EC29 -and -not $isC011EC30) {
            $c26PreflightStart = $validationText.IndexOf('[nativeaot-gc-stack-completion] preflight marker=C011EC26-PREFLIGHT')
            $c26CompleteStart = $validationText.IndexOf('[nativeaot-gc-stack-completion] COMPLETE marker=C011EC26')
            $c28PreflightStart = $validationText.IndexOf('[nativeaot-gc-mark-queue-closure] preflight marker=C011EC28-PREFLIGHT')
            $c28CompleteStart = $validationText.IndexOf('[nativeaot-gc-mark-queue-closure] COMPLETE marker=C011EC28')
            $c29PreflightStart = $validationText.IndexOf('[nativeaot-gc-post-mark-phase] preflight marker=C011EC29-PREFLIGHT')
            $c29CompleteStart = $validationText.IndexOf('[nativeaot-gc-post-mark-phase] COMPLETE marker=C011EC29')
            $c29BlockedStart = $validationText.IndexOf('[nativeaot-gc-post-mark-phase] BLOCKED')
            $c26PreflightLine = if ($c26PreflightStart -ge 0 -and $c26CompleteStart -gt $c26PreflightStart) { $validationText.Substring($c26PreflightStart, $c26CompleteStart - $c26PreflightStart) } else { $null }
            $c26MarkerLine = if ($c26CompleteStart -ge 0 -and $c28PreflightStart -gt $c26CompleteStart) { $validationText.Substring($c26CompleteStart, $c28PreflightStart - $c26CompleteStart) } else { $null }
            if ($c29CompleteStart -lt 0) {
                if ($c29BlockedStart -ge 0) { $runResults += [ordered]@{ name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker='C011EC29-BLOCKED'; outcome='H / post-mark blocker'; successLevel=0; harnessTerminated=$true; markerLine=$validationText.Substring($c29BlockedStart); c26MarkerLine=$c26MarkerLine; c26PreflightLine=$c26PreflightLine }; continue }
                throw "C011EC29 preflight/completion evidence was incomplete in $name."
            }
            $c28PreflightLine = if ($c28PreflightStart -ge 0 -and $c28CompleteStart -gt $c28PreflightStart) { $validationText.Substring($c28PreflightStart, $c28CompleteStart - $c28PreflightStart) } else { $null }
            $c28MarkerLine = if ($c28CompleteStart -ge 0 -and $c29PreflightStart -gt $c28CompleteStart) { $validationText.Substring($c28CompleteStart, $c29PreflightStart - $c28CompleteStart) } else { $null }
            $c29PreflightLine = if ($c29PreflightStart -ge 0 -and $c29CompleteStart -gt $c29PreflightStart) { $validationText.Substring($c29PreflightStart, $c29CompleteStart - $c29PreflightStart) } else { $null }
            $c29MarkerLine = $validationText.Substring($c29CompleteStart)
            if (@($c26PreflightLine,$c26MarkerLine,$c28PreflightLine,$c28MarkerLine,$c29PreflightLine | Where-Object { [string]::IsNullOrWhiteSpace($_) }).Count -ne 0) { throw "C011EC29 retained C26/C28 or C29 preflight evidence was incomplete in $name." }
            foreach ($expected in @(@('preflightProven','0x00000001'),@('iteratorCompletionCount','0x00000001'),@('gcScanRootsEntries','0x00000001'),@('gcScanRootsReturns','0x00000001'),@('rootEnumerationComplete','0x00000001'),@('nativeUnwindCount','0x00000002'),@('thirdUnwindAttempts','0x00000000'),@('stackBoundsConsumed','0x00000000'),@('totalRoots','0x00000007'),@('threadStoreLockHeld','0x00000001'),@('eeSuspended','0x00000001'),@('managedEntryProhibited','0x00000001'),@('cooperative','0x00000001'),@('preemptive','0x00000000'),@('restart','0x00000000'),@('resume','0x00000000'),@('safeStopReason','0x00000000'))) { if ((Get-MarkerField $c26MarkerLine $expected[0]) -ne $expected[1]) { throw "C011EC29 retained C26 expected $($expected[0])=$($expected[1]) in $name." } }
            $c28Read = { param([string]$Line,[string]$Field) $v=Get-MarkerField $Line $Field; if($null -eq $v){throw "C011EC29 missing C28 field $Field."}; [Convert]::ToUInt64($v.Substring(2),16) }
            foreach ($check in @(@('c26Completion',1),@('queueCapacity',16),@('queueInvariantFailures',0),@('finalCount',0),@('queueFinalOccupancy',0),@('finalEmptyResult',1),@('finalDrainEmptyResult',1),@('queueSemanticsValidated',1),@('eeSuspended',1),@('cooperative',1),@('preemptive',0),@('threadStoreLockHeld',1),@('managedEntryProhibited',1),@('restart',0),@('resume',0),@('safeStopReason',0))) { if((& $c28Read $c28MarkerLine $check[0]) -ne [uint64]$check[1]){throw "C011EC29 retained C28 expected $($check[0])=$($check[1]) in $name."} }
            foreach ($field in @('drainEntries','drainReturns','successfulDequeues','enqueueAttempts','successfulEnqueues','markTests','newlyMarked','markWrites','objectsScanned','referenceSlots','childPromoteAttempts','childQueueMarkEntries','childQueueMarkReturns')) { if((& $c28Read $c28MarkerLine $field) -eq 0){throw "C011EC29 retained C28 activity $field was zero in $name."} }
            if((& $c28Read $c28MarkerLine 'nullReferences') + (& $c28Read $c28MarkerLine 'nonNullReferences') -ne (& $c28Read $c28MarkerLine 'referenceSlots')){throw "C011EC29 retained C28 child accounting did not close in $name."}
            $c29Read = { param([string]$Line,[string]$Field) $v=Get-MarkerField $Line $Field; if($null -eq $v){throw "C011EC29 missing field $Field."}; [Convert]::ToUInt64($v.Substring(2),16) }
            foreach ($check in @(@('successLevel',1),@('c011ec29Preflight',1),@('afterGcScanRootsEntries',1),@('afterGcScanRootsReturns',1),@('nextPhaseEntries',1),@('shortWeakHandleScanEntries',1),@('handleMapReads',1),@('firstMutationAttempted',0),@('finalizationReached',0),@('planReached',0),@('sweepReached',0),@('relocationReached',0),@('restartPreparationReached',0),@('eeSuspended',1),@('threadStoreLockHeld',1),@('threadStoreRecursion',1),@('cooperative',1),@('preemptive',0),@('managedEntryProhibited',1),@('managedEntryAttempts',0),@('sensitiveAllocations',0),@('stackBoundsConsumed',0),@('queuePendingAtTransition',0),@('markPendingAtTransition',0),@('restart',0),@('resume',0),@('safeStopReason',0))) { if((& $c29Read $c29MarkerLine $check[0]) -ne [uint64]$check[1]){throw "C011EC29 expected $($check[0])=$($check[1]) in $name."} }
            foreach ($field in @('firstHandleTableMapAddress','firstHandleTableMapBucketsFieldAddress','firstOperationAddress')) { if((& $c29Read $c29MarkerLine $field) -eq 0){throw "C011EC29 expected nonzero $field in $name."} }
            $runResults += [ordered]@{ name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker='C011EC29'; outcome='A / first post-mark phase entered; first HandleTableMap read captured'; successLevel=1; harnessTerminated=$true; markerLine=$c29MarkerLine; preflightLine=$c29PreflightLine; c28MarkerLine=$c28MarkerLine; c28PreflightLine=$c28PreflightLine; c26MarkerLine=$c26MarkerLine; c26PreflightLine=$c26PreflightLine }
        } elseif ($isC011EC28) {
            $c26PreflightStart = $validationText.IndexOf('[nativeaot-gc-stack-completion] preflight marker=C011EC26-PREFLIGHT')
            $c26CompleteStart = $validationText.IndexOf('[nativeaot-gc-stack-completion] COMPLETE marker=C011EC26')
            $c28PreflightStart = $validationText.IndexOf('[nativeaot-gc-mark-queue-closure] preflight marker=C011EC28-PREFLIGHT')
            $c28CompleteStart = $validationText.IndexOf('[nativeaot-gc-mark-queue-closure] COMPLETE marker=C011EC28')
            $c28BlockedStart = $validationText.IndexOf('[nativeaot-gc-mark-queue-closure] BLOCKED')
            $c26PreflightLine = if ($c26PreflightStart -ge 0 -and $c26CompleteStart -gt $c26PreflightStart) { $validationText.Substring($c26PreflightStart, $c26CompleteStart - $c26PreflightStart) } else { $null }
            $c26MarkerLine = if ($c26CompleteStart -ge 0 -and $c28PreflightStart -gt $c26CompleteStart) { $validationText.Substring($c26CompleteStart, $c28PreflightStart - $c26CompleteStart) } else { $null }
            if ($c28CompleteStart -lt 0) {
                if ($c28BlockedStart -ge 0) {
                    $runResults += [ordered]@{ name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker='C011EC28-BLOCKED'; outcome='E'; successLevel=0; harnessTerminated=$true; markerLine=$validationText.Substring($c28BlockedStart); c26MarkerLine=$c26MarkerLine; c26PreflightLine=$c26PreflightLine }
                    continue
                }
                throw "C011EC28 preflight/completion evidence was incomplete in $name."
            }
            $c28PreflightLine = if ($c28PreflightStart -ge 0 -and $c28CompleteStart -gt $c28PreflightStart) { $validationText.Substring($c28PreflightStart, $c28CompleteStart - $c28PreflightStart) } else { $null }
            $c28MarkerLine = $validationText.Substring($c28CompleteStart)
            if ([string]::IsNullOrWhiteSpace($c26PreflightLine) -or [string]::IsNullOrWhiteSpace($c26MarkerLine) -or [string]::IsNullOrWhiteSpace($c28PreflightLine)) { throw "C011EC28 retained C26 or preflight evidence was incomplete in $name." }
            foreach ($expected in @(@('preflightProven','0x00000001'),@('terminalDescriptorValid','0x00000001'),@('terminalLookupSuccesses','0x00000001'),@('iteratorCompletionCount','0x00000001'),@('stackProviderCallbackEntries','0x00000001'),@('stackProviderCallbackReturns','0x00000001'),@('gcScanRootsEntries','0x00000001'),@('gcScanRootsReturns','0x00000001'),@('threadGcScanRootsEntries','0x00000001'),@('threadGcScanRootsReturns','0x00000001'),@('rootEnumerationComplete','0x00000001'),@('nativeUnwindCount','0x00000002'),@('thirdUnwindAttempts','0x00000000'),@('iteratorFrames','0x00000001'),@('managedFrames','0x00000001'),@('stackBoundsConsumed','0x00000000'),@('totalRoots','0x00000007'),@('category3Roots','0x00000004'),@('registerRoots','0x00000003'),@('stackRoots','0x00000001'),@('promoteAttempts','0x00000006'),@('promoteEntries','0x00000006'),@('promoteReturns','0x00000005'),@('firstPostScanEvent','0x00000001'),@('firstPostScanQueueOperation','0x00000000'),@('markWrites','0x00000000'),@('childReads','0x00000000'),@('graphTraversal','0x00000000'),@('threadStoreLockHeld','0x00000001'),@('eeSuspended','0x00000001'),@('managedEntryProhibited','0x00000001'),@('cooperative','0x00000001'),@('preemptive','0x00000000'),@('threadUnderCrawl','0x00000000'),@('restart','0x00000000'),@('resume','0x00000000'),@('safeStopReason','0x00000000'))) { if ((Get-MarkerField $c26MarkerLine $expected[0]) -ne $expected[1]) { throw "C011EC28 retained C26 expected $($expected[0])=$($expected[1]) in $name." } }
            foreach ($field in @('c26Completion','c27FirstDequeue','c27FirstMarkWrite','c27FirstChildRead','queueSemanticsValidated','queueInvariantFailures','objectInvariantFailures','queueOwner','queueBase','firstObject','firstChild')) { if ($null -eq (Get-MarkerField $c28PreflightLine $field)) { throw "C011EC28 preflight field $field was missing in $name." } }
            $c28Fields = @('c26Completion','firstObject','firstChildValue','firstObjectMarkMask','queueCapacity','initialHead','initialTail','initialCount','initialQueueBase','finalHead','finalTail','finalCount','finalQueueBase','drainEntries','drainReturns','dequeueAttempts','successfulDequeues','enqueueAttempts','successfulEnqueues','alreadyMarkedSkips','wraps','displacements','queueFullCount','queueFullResolved','displacementResolved','displacementPending','maxOccupancy','queueFinalOccupancy','queueInvariantFailures','emptyTests','finalEmptyResult','finalDrainEmptyTests','finalDrainEmptyResult','markTests','alreadyMarked','newlyMarked','markWrites','objectsScanned','referenceSlots','nullReferences','nonNullReferences','childPromoteAttempts','childQueueMarkEntries','childQueueMarkReturns','childQueueInsertions','finalDequeuedObject','finalDequeuedSlot','finalDequeuedIndex','finalObject','finalObjectMarkState','finalObjectNewlyMarked','finalObjectChildSlots','finalObjectChildEnqueues','finalObjectMarkWordAddress','finalObjectMarkWordBefore','finalObjectMarkWordAfter','finalObjectMarkMask','laterObject','laterObjectMarkWordAddress','laterObjectMarkWordBefore','laterObjectMarkWordAfter','laterObjectMarkMask','firstScanParent','firstScanMethodTable','firstScanFirstChild','laterScanParent','laterScanMethodTable','finalScanParent','finalScanMethodTable','finalScanFirstChild','queueSemanticsValidated','nextProductionBoundary','nextProductionBoundaryAddress','stackBase','stackLimit','scanContextStackLimit','stackBoundsConsumed','eeSuspended','cooperative','preemptive','threadStoreLockHeld','managedEntryProhibited','threadUnderCrawl','restart','resume','safeStopReason')
            foreach ($field in $c28Fields) { if ($null -eq (Get-MarkerField $c28MarkerLine $field)) { throw "C011EC28 field $field was missing in $name." } }
            $c28Read = { param([string]$Line,[string]$Field) $v=Get-MarkerField $Line $Field; if($null -eq $v){throw "C011EC28 numeric field $Field was missing."}; [Convert]::ToUInt64($v.Substring(2),16) }
            foreach ($check in @(@('c26Completion',1),@('queueCapacity',16),@('queueInvariantFailures',0),@('displacementPending',0),@('finalCount',0),@('queueFinalOccupancy',0),@('finalEmptyResult',1),@('finalDrainEmptyResult',1),@('nextProductionBoundary',1),@('stackBoundsConsumed',0),@('eeSuspended',1),@('cooperative',1),@('preemptive',0),@('threadStoreLockHeld',1),@('managedEntryProhibited',1),@('threadUnderCrawl',0),@('restart',0),@('resume',0),@('safeStopReason',0))) { if((& $c28Read $c28MarkerLine $check[0]) -ne [uint64]$check[1]){throw "C011EC28 expected $($check[0])=$($check[1]) in $name."} }
            foreach ($field in @('drainEntries','drainReturns','successfulDequeues','enqueueAttempts','successfulEnqueues','emptyTests','finalDrainEmptyTests','markTests','newlyMarked','markWrites','objectsScanned','referenceSlots','childPromoteAttempts','childQueueMarkEntries','childQueueMarkReturns')) { if((& $c28Read $c28MarkerLine $field) -eq 0){throw "C011EC28 required genuine activity $field was zero in $name."} }
            foreach ($field in @('initialHead','initialTail','finalHead','finalTail')) { if((& $c28Read $c28MarkerLine $field) -ge 16){throw "C011EC28 ring index $field was outside [0,15] in $name."} }
            foreach ($field in @('initialCount','finalCount','maxOccupancy','queueFinalOccupancy')) { if((& $c28Read $c28MarkerLine $field) -gt 16){throw "C011EC28 occupancy $field exceeded capacity in $name."} }
            if((& $c28Read $c28MarkerLine 'displacementResolved') -ne (& $c28Read $c28MarkerLine 'displacements')){throw "C011EC28 displacement accounting did not close in $name."}
            if((& $c28Read $c28MarkerLine 'queueFullResolved') -ne (& $c28Read $c28MarkerLine 'queueFullCount')){throw "C011EC28 queue-full accounting did not close in $name."}
            if((& $c28Read $c28MarkerLine 'nullReferences') + (& $c28Read $c28MarkerLine 'nonNullReferences') -ne (& $c28Read $c28MarkerLine 'referenceSlots')){throw "C011EC28 child null/non-null accounting did not close in $name."}
            if((& $c28Read $c28MarkerLine 'childQueueMarkEntries') -ne (& $c28Read $c28MarkerLine 'childQueueMarkReturns')){throw "C011EC28 child queue_mark entry/return accounting did not close in $name."}
            foreach ($expected in @(@('firstObject','0x0000000100A02F50'),@('firstChildValue','0x0000000100A01F38'),@('firstObjectMarkMask','0x0000000000000001'))) { if((Get-MarkerField $c28MarkerLine $expected[0]) -ne $expected[1]){throw "C011EC28 retained C27 $($expected[0])=$($expected[1]) in $name."} }
            foreach ($field in @('laterObject','laterObjectMarkWordAddress','laterObjectMarkWordAfter','laterObjectMarkMask','finalObject','finalDequeuedObject','finalObjectMarkWordAddress','finalObjectMarkMask','nextProductionBoundaryAddress')) { if((& $c28Read $c28MarkerLine $field) -eq 0){throw "C011EC28 representative/final field $field was zero in $name."} }
            $runResults += [ordered]@{ name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker='C011EC28'; outcome='A / authentic Workstation mark queue closure and first post-mark boundary'; successLevel=4; harnessTerminated=$true; markerLine=$c28MarkerLine; preflightLine=$c28PreflightLine; c26MarkerLine=$c26MarkerLine; c26PreflightLine=$c26PreflightLine }
        } elseif ($isC011EC27Stop) {
            $c26PreflightStart = $validationText.IndexOf('[nativeaot-gc-stack-completion] preflight marker=C011EC26-PREFLIGHT')
            $c26CompleteStart = $validationText.IndexOf('[nativeaot-gc-stack-completion] COMPLETE marker=C011EC26')
            $c27PreflightStart = $validationText.IndexOf('[nativeaot-gc-post-root-queue] preflight marker=C011EC27-PREFLIGHT')
            $c27CompleteStart = $validationText.IndexOf('[nativeaot-gc-post-root-queue] COMPLETE marker=C011EC27')
            $c27BlockedStart = $validationText.IndexOf('[nativeaot-gc-post-root-queue] BLOCKED')
            $c26PreflightLine = if ($c26PreflightStart -ge 0 -and $c26CompleteStart -gt $c26PreflightStart) { $validationText.Substring($c26PreflightStart, $c26CompleteStart - $c26PreflightStart) } else { $null }
            $c26MarkerLine = if ($c26CompleteStart -ge 0 -and $c27PreflightStart -gt $c26CompleteStart) { $validationText.Substring($c26CompleteStart, $c27PreflightStart - $c26CompleteStart) } else { $null }
            if ($c27CompleteStart -lt 0) {
                if ($c27BlockedStart -ge 0) {
                    $runResults += [ordered]@{ name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker='C011EC27-BLOCKED'; outcome='E'; successLevel=0; harnessTerminated=$true; markerLine=$validationText.Substring($c27BlockedStart); c26MarkerLine=$c26MarkerLine; c26PreflightLine=$c26PreflightLine }
                    continue
                }
                throw "C011EC27 preflight/completion evidence was incomplete in $name."
            }
            $c27PreflightLine = if ($c27PreflightStart -ge 0 -and $c27CompleteStart -gt $c27PreflightStart) { $validationText.Substring($c27PreflightStart, $c27CompleteStart - $c27PreflightStart) } else { $null }
            $c27MarkerLine = $validationText.Substring($c27CompleteStart)
            if ([string]::IsNullOrWhiteSpace($c26PreflightLine) -or [string]::IsNullOrWhiteSpace($c26MarkerLine) -or [string]::IsNullOrWhiteSpace($c27PreflightLine)) { throw "C011EC27 retained C26 or preflight evidence was incomplete in $name." }
            foreach ($expected in @(
                @('preflightProven','0x00000001'), @('terminalDescriptorValid','0x00000001'), @('terminalLookupSuccesses','0x00000001'),
                @('iteratorCompletionCount','0x00000001'), @('stackProviderCallbackEntries','0x00000001'), @('stackProviderCallbackReturns','0x00000001'),
                @('gcScanRootsEntries','0x00000001'), @('gcScanRootsReturns','0x00000001'), @('threadGcScanRootsEntries','0x00000001'), @('threadGcScanRootsReturns','0x00000001'),
                @('rootEnumerationComplete','0x00000001'), @('nativeUnwindCount','0x00000002'), @('thirdUnwindAttempts','0x00000000'), @('iteratorFrames','0x00000001'),
                @('managedFrames','0x00000001'), @('stackBoundsConsumed','0x00000000'), @('totalRoots','0x00000007'), @('category3Roots','0x00000004'),
                @('registerRoots','0x00000003'), @('stackRoots','0x00000001'), @('promoteAttempts','0x00000006'), @('promoteEntries','0x00000006'), @('promoteReturns','0x00000005'),
                @('firstPostScanEvent','0x00000001'), @('firstPostScanQueueOperation','0x00000000'), @('markWrites','0x00000000'), @('childReads','0x00000000'), @('graphTraversal','0x00000000'),
                @('threadStoreLockHeld','0x00000001'), @('eeSuspended','0x00000001'), @('managedEntryProhibited','0x00000001'), @('cooperative','0x00000001'), @('preemptive','0x00000000'),
                @('threadUnderCrawl','0x00000000'), @('restart','0x00000000'), @('resume','0x00000000'), @('safeStopReason','0x00000000'))) {
                if ((Get-MarkerField $c26MarkerLine $expected[0]) -ne $expected[1]) { throw "C011EC27 retained C26 expected $($expected[0])=$($expected[1]) in $name." }
            }
            foreach ($field in @('c26Completion','rootEnumerationComplete','afterGcScanRoots','queueItemsConsumed','markStateReads','queueOwner','queueBase','consumedSlot','consumedObject','markMask')) {
                if ($null -eq (Get-MarkerField $c27PreflightLine $field)) { throw "C011EC27 preflight field $field was missing in $name." }
            }
            foreach ($field in @('successLevel','c26Completion','iteratorCompletionCount','gcScanRootsEntries','gcScanRootsReturns','rootEnumerationComplete','totalRoots','category3Roots','registerRoots','stackRoots','promoteAttempts','promoteEntries','promoteReturns','firstPostStackRootSource','postStackRootSourceCount','queueCursorBeforeStack','queueCursorAfterStack','queueCursorAtGcScanRootsReturn','afterGcScanRoots','afterGcScanRootsAddress','queueItemsConsumed','queueOwner','queueBase','queueCursorBefore','consumedIndex','consumedSlot','consumedObject','firstQueueInsertionObject','firstRootValue','firstRootProviderCategory','consumedObjectSourceCategory','sentinel','storageObject','consumedSlotValueAfter','queueCursorAfterConsumption','queueInsertionsAtConsumed','queueInsertionsAtAfter','newQueueInsertion','markStateReads','markStateBefore','markTestResult','markWordAddress','markWordBefore','markMask','markWriteAttempted','markWrites','markWordAfter','newlyMarked','childScanAttempted','childReads','childPromoteAttempted','graphTraversal','parentObject','parentMethodTable','childSlot','childValue','c26NativeUnwinds','c26ThirdUnwindAttempts','stackBoundsConsumed','stackBase','stackLimit','scanContextStackLimit','queueInvariantFailures','objectInvariantFailures','eeSuspended','cooperative','preemptive','restart','resume','safeStopReason')) {
                if ($null -eq (Get-MarkerField $c27MarkerLine $field)) { throw "C011EC27 field $field was missing in $name." }
            }
            foreach ($expected in @(
                @('c26Completion','0x00000001'), @('iteratorCompletionCount','0x00000001'), @('gcScanRootsEntries','0x00000001'), @('gcScanRootsReturns','0x00000001'), @('rootEnumerationComplete','0x00000001'),
                @('totalRoots','0x00000007'), @('category3Roots','0x00000004'), @('registerRoots','0x00000003'), @('stackRoots','0x00000001'), @('promoteAttempts','0x00000006'), @('promoteEntries','0x00000006'), @('promoteReturns','0x00000005'),
                @('afterGcScanRoots','0x00000001'), @('markMask','0x0000000000000001'), @('queueInvariantFailures','0x00000000'), @('objectInvariantFailures','0x00000000'),
                @('eeSuspended','0x00000001'), @('cooperative','0x00000001'), @('preemptive','0x00000000'), @('restart','0x00000000'), @('resume','0x00000000'), @('safeStopReason','0x00000000'))) {
                if ((Get-MarkerField $c27MarkerLine $expected[0]) -ne $expected[1]) { throw "C011EC27 expected $($expected[0])=$($expected[1]) in $name." }
            }
            $queueItemsConsumed = [Convert]::ToUInt32((Get-MarkerField $c27MarkerLine 'queueItemsConsumed').Substring(2), 16)
            $markStateReads = [Convert]::ToUInt32((Get-MarkerField $c27MarkerLine 'markStateReads').Substring(2), 16)
            if ($queueItemsConsumed -lt 1 -or $markStateReads -lt 1) { throw "C011EC27 did not consume a queue item and resolve mark state in $name." }
            $successLevel = [Convert]::ToUInt32((Get-MarkerField $c27MarkerLine 'successLevel').Substring(2), 16)
            if ($successLevel -lt 1 -or $successLevel -gt 3) { throw "C011EC27 success level was outside 1..3 in $name." }
            $outcome = if ($successLevel -eq 3) { 'C / first child/reference slot read' } elseif ($successLevel -eq 2) { 'B / first genuine mark write' } else { 'A / first real post-root queue item consumed' }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker='C011EC27'; outcome=$outcome; successLevel=$successLevel; harnessTerminated=$true; markerLine=$c27MarkerLine; preflightLine=$c27PreflightLine; c26MarkerLine=$c26MarkerLine; c26PreflightLine=$c26PreflightLine
                c26=[ordered]@{ iteratorCompletionCount=(Get-MarkerField $c26MarkerLine 'iteratorCompletionCount'); gcScanRootsReturns=(Get-MarkerField $c26MarkerLine 'gcScanRootsReturns'); totalRoots=(Get-MarkerField $c26MarkerLine 'totalRoots'); stackScanTotalRoots=(Get-MarkerField $c26MarkerLine 'stackScanTotalRoots'); postStackSource=(Get-MarkerField $c26MarkerLine 'firstPostStackRootSource'); postStackSourceCount=(Get-MarkerField $c26MarkerLine 'postStackRootSourceCount') }
                queue=[ordered]@{ base=(Get-MarkerField $c27MarkerLine 'queueBase'); owner=(Get-MarkerField $c27MarkerLine 'queueOwner'); cursorBefore=(Get-MarkerField $c27MarkerLine 'queueCursorBefore'); consumedIndex=(Get-MarkerField $c27MarkerLine 'consumedIndex'); consumedSlot=(Get-MarkerField $c27MarkerLine 'consumedSlot'); consumedObject=(Get-MarkerField $c27MarkerLine 'consumedObject'); slotValueAfter=(Get-MarkerField $c27MarkerLine 'consumedSlotValueAfter'); cursorAfter=(Get-MarkerField $c27MarkerLine 'queueCursorAfterConsumption'); insertionsAtConsumed=(Get-MarkerField $c27MarkerLine 'queueInsertionsAtConsumed'); insertionsAtAfter=(Get-MarkerField $c27MarkerLine 'queueInsertionsAtAfter'); newInsertion=(Get-MarkerField $c27MarkerLine 'newQueueInsertion'); sourceCategory=(Get-MarkerField $c27MarkerLine 'consumedObjectSourceCategory') }
                mark=[ordered]@{ wordAddress=(Get-MarkerField $c27MarkerLine 'markWordAddress'); wordBefore=(Get-MarkerField $c27MarkerLine 'markWordBefore'); mask=(Get-MarkerField $c27MarkerLine 'markMask'); stateBefore=(Get-MarkerField $c27MarkerLine 'markStateBefore'); test=(Get-MarkerField $c27MarkerLine 'markTestResult'); writeAttempted=(Get-MarkerField $c27MarkerLine 'markWriteAttempted'); writes=(Get-MarkerField $c27MarkerLine 'markWrites'); wordAfter=(Get-MarkerField $c27MarkerLine 'markWordAfter'); newlyMarked=(Get-MarkerField $c27MarkerLine 'newlyMarked') }
                child=[ordered]@{ scanAttempted=(Get-MarkerField $c27MarkerLine 'childScanAttempted'); parent=(Get-MarkerField $c27MarkerLine 'parentObject'); methodTable=(Get-MarkerField $c27MarkerLine 'parentMethodTable'); slot=(Get-MarkerField $c27MarkerLine 'childSlot'); value=(Get-MarkerField $c27MarkerLine 'childValue'); reads=(Get-MarkerField $c27MarkerLine 'childReads'); promoteAttempted=(Get-MarkerField $c27MarkerLine 'childPromoteAttempted') }
                invariants=[ordered]@{ queue=(Get-MarkerField $c27MarkerLine 'queueInvariantFailures'); object=(Get-MarkerField $c27MarkerLine 'objectInvariantFailures'); sentinel=(Get-MarkerField $c27MarkerLine 'sentinel'); storageObject=(Get-MarkerField $c27MarkerLine 'storageObject') }
            }
        } elseif ($isC011EC26) {
            $c26PreflightStart = $validationText.IndexOf(
                '[nativeaot-gc-stack-completion] preflight marker=C011EC26-PREFLIGHT')
            $c26CompleteStart = $validationText.IndexOf(
                '[nativeaot-gc-stack-completion] COMPLETE marker=C011EC26')
            $c26PreflightLine = if ($c26PreflightStart -ge 0 -and $c26CompleteStart -gt $c26PreflightStart) {
                $validationText.Substring($c26PreflightStart, $c26CompleteStart - $c26PreflightStart)
            } else { $null }
            $c26MarkerLine = if ($c26CompleteStart -ge 0) {
                $validationText.Substring($c26CompleteStart)
            } else { $null }
            if ([string]::IsNullOrWhiteSpace($c26PreflightLine) -or
                [string]::IsNullOrWhiteSpace($c26MarkerLine)) {
                throw "C011EC26 preflight/completion evidence was incomplete in $name."
            }
            foreach ($field in @(
                'terminalClassification','terminalDescriptorValid','terminalLookupAttempts',
                'terminalLookupSuccesses','nativeUnwindCount','nativeFramesCrossed',
                'thirdUnwindAttempts','terminalInputPC','terminalSelectedPC','terminalLinkedPC',
                'terminalModuleBase','terminalExecutableStart','terminalExecutableEnd',
                'terminalBeginRVA','terminalEndRVA','terminalRSP')) {
                if ($null -eq (Get-MarkerField $c26PreflightLine $field)) {
                    throw "C011EC26 preflight field $field was missing in $name."
                }
            }
            foreach ($field in @(
                'preflightProven','terminalClassification','terminalDescriptorValid',
                'terminalLookupAttempts','terminalLookupSuccesses','iteratorCompletionCount',
                'stackProviderCallbackEntries','stackProviderCallbackReturns','gcScanRootsEntries',
                'gcScanRootsReturns','threadGcScanRootsEntries','threadGcScanRootsReturns',
                 'rootEnumerationComplete','nativeUnwindCount','thirdUnwindAttempts',
                 'iteratorFrames','managedFrames','stackBoundsConsumed','totalRoots','category3Roots','registerRoots',
                 'stackRoots','promoteAttempts','promoteEntries','promoteReturns',
                 'firstPostScanEvent','firstPostScanQueueOperation','firstPostStackRootSource',
                 'postStackRootSourceCount','stackScanTotalRoots','stackScanCategory3Roots',
                 'stackScanRegisterRoots','stackScanStackRoots','stackScanPromoteAttempts',
                 'stackScanPromoteEntries','stackScanPromoteReturns','markWrites','childReads',
                'graphTraversal','threadStoreLockHeld','eeSuspended','managedEntryProhibited',
                'cooperative','preemptive','threadUnderCrawl','restart','resume','safeStopReason',
                'terminalInputPC','terminalSelectedPC','terminalLinkedPC','terminalModuleBase',
                'terminalExecutableStart','terminalExecutableEnd','terminalBeginRVA',
                'terminalEndRVA','terminalRSP','postScanAddress','stackBase','stackLimit',
                'scanContextStackLimit','queueCursorBeforeStack','queueCursorAfterStack',
                'queueCursorAtGcScanRootsReturn')) {
                if ($null -eq (Get-MarkerField $c26MarkerLine $field)) {
                    throw "C011EC26 field $field was missing in $name."
                }
            }
            foreach ($expected in @(
                @('preflightProven','0x00000001'),
                @('terminalClassification','0x00000001'),
                @('terminalDescriptorValid','0x00000001'),
                @('terminalLookupSuccesses','0x00000001'),
                @('iteratorCompletionCount','0x00000001'),
                @('stackProviderCallbackEntries','0x00000001'),
                @('stackProviderCallbackReturns','0x00000001'),
                @('gcScanRootsEntries','0x00000001'),
                @('gcScanRootsReturns','0x00000001'),
                @('threadGcScanRootsEntries','0x00000001'),
                @('threadGcScanRootsReturns','0x00000001'),
                @('rootEnumerationComplete','0x00000001'),
                @('nativeUnwindCount','0x00000002'),
                @('thirdUnwindAttempts','0x00000000'),
                @('iteratorFrames','0x00000001'),
                @('managedFrames','0x00000001'),
                @('stackBoundsConsumed','0x00000000'),
                 @('totalRoots','0x00000007'),
                 @('category3Roots','0x00000004'),
                 @('registerRoots','0x00000003'),
                 @('stackRoots','0x00000001'),
                 @('promoteAttempts','0x00000006'),
                 @('promoteEntries','0x00000006'),
                 @('promoteReturns','0x00000005'),
                 @('stackScanTotalRoots','0x00000006'),
                 @('stackScanCategory3Roots','0x00000004'),
                 @('stackScanRegisterRoots','0x00000003'),
                 @('stackScanStackRoots','0x00000001'),
                 @('stackScanPromoteAttempts','0x00000004'),
                 @('stackScanPromoteEntries','0x00000004'),
                 @('stackScanPromoteReturns','0x00000004'),
                @('firstPostScanEvent','0x00000001'),
                @('firstPostScanQueueOperation','0x00000000'),
                @('markWrites','0x00000000'),
                @('childReads','0x00000000'),
                @('graphTraversal','0x00000000'),
                @('threadStoreLockHeld','0x00000001'),
                @('eeSuspended','0x00000001'),
                @('managedEntryProhibited','0x00000001'),
                @('cooperative','0x00000001'),
                @('preemptive','0x00000000'),
                @('threadUnderCrawl','0x00000000'),
                @('restart','0x00000000'),
                @('resume','0x00000000'),
                @('safeStopReason','0x00000000'))) {
                 if ((Get-MarkerField $c26MarkerLine $expected[0]) -ne $expected[1]) {
                     throw "C011EC26 expected $($expected[0])=$($expected[1]) in $name."
                 }
             }
             $postStackSource = [Convert]::ToUInt32((Get-MarkerField $c26MarkerLine 'firstPostStackRootSource').Substring(2), 16)
             $postStackSourceCount = [Convert]::ToUInt32((Get-MarkerField $c26MarkerLine 'postStackRootSourceCount').Substring(2), 16)
             if ($postStackSource -lt 1 -or $postStackSource -gt 3 -or $postStackSourceCount -lt 1) {
                 throw "C011EC26 did not identify a post-stack NativeAOT root source in $name."
             }
            $terminalAttempts = [Convert]::ToUInt32((Get-MarkerField $c26MarkerLine 'terminalLookupAttempts').Substring(2), 16)
            if ($terminalAttempts -lt 3) { throw "C011EC26 did not attempt terminal classification after both native frames in $name." }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath);
                safeStopMarker='C011EC26'; outcome='A'; harnessTerminated=$true;
                markerLine=$c26MarkerLine; preflightLine=$c26PreflightLine;
                terminal=[ordered]@{
                    classification=(Get-MarkerField $c26MarkerLine 'terminalClassification'); descriptorValid=(Get-MarkerField $c26MarkerLine 'terminalDescriptorValid');
                    attempts=(Get-MarkerField $c26MarkerLine 'terminalLookupAttempts'); successes=(Get-MarkerField $c26MarkerLine 'terminalLookupSuccesses');
                    inputPc=(Get-MarkerField $c26MarkerLine 'terminalInputPC'); selectedPc=(Get-MarkerField $c26MarkerLine 'terminalSelectedPC'); linkedPc=(Get-MarkerField $c26MarkerLine 'terminalLinkedPC');
                    moduleBase=(Get-MarkerField $c26MarkerLine 'terminalModuleBase'); executableStart=(Get-MarkerField $c26MarkerLine 'terminalExecutableStart'); executableEnd=(Get-MarkerField $c26MarkerLine 'terminalExecutableEnd');
                    beginRva=(Get-MarkerField $c26MarkerLine 'terminalBeginRVA'); endRva=(Get-MarkerField $c26MarkerLine 'terminalEndRVA'); rsp=(Get-MarkerField $c26MarkerLine 'terminalRSP')
                };
                completion=[ordered]@{
                    iterator=(Get-MarkerField $c26MarkerLine 'iteratorCompletionCount'); stackProviderEntries=(Get-MarkerField $c26MarkerLine 'stackProviderCallbackEntries'); stackProviderReturns=(Get-MarkerField $c26MarkerLine 'stackProviderCallbackReturns');
                    gcScanRootsEntries=(Get-MarkerField $c26MarkerLine 'gcScanRootsEntries'); gcScanRootsReturns=(Get-MarkerField $c26MarkerLine 'gcScanRootsReturns');
                    threadGcScanRootsEntries=(Get-MarkerField $c26MarkerLine 'threadGcScanRootsEntries'); threadGcScanRootsReturns=(Get-MarkerField $c26MarkerLine 'threadGcScanRootsReturns'); rootEnumerationComplete=(Get-MarkerField $c26MarkerLine 'rootEnumerationComplete'); postScanAddress=(Get-MarkerField $c26MarkerLine 'postScanAddress')
                };
                roots=[ordered]@{ total=(Get-MarkerField $c26MarkerLine 'totalRoots'); category3=(Get-MarkerField $c26MarkerLine 'category3Roots'); register=(Get-MarkerField $c26MarkerLine 'registerRoots'); stack=(Get-MarkerField $c26MarkerLine 'stackRoots'); promoteAttempts=(Get-MarkerField $c26MarkerLine 'promoteAttempts'); promoteEntries=(Get-MarkerField $c26MarkerLine 'promoteEntries'); promoteReturns=(Get-MarkerField $c26MarkerLine 'promoteReturns') }; stackScan=[ordered]@{ total=(Get-MarkerField $c26MarkerLine 'stackScanTotalRoots'); category3=(Get-MarkerField $c26MarkerLine 'stackScanCategory3Roots'); register=(Get-MarkerField $c26MarkerLine 'stackScanRegisterRoots'); stack=(Get-MarkerField $c26MarkerLine 'stackScanStackRoots'); promoteAttempts=(Get-MarkerField $c26MarkerLine 'stackScanPromoteAttempts'); promoteEntries=(Get-MarkerField $c26MarkerLine 'stackScanPromoteEntries'); promoteReturns=(Get-MarkerField $c26MarkerLine 'stackScanPromoteReturns') }; postStack=[ordered]@{ firstSource=(Get-MarkerField $c26MarkerLine 'firstPostStackRootSource'); sourceCount=(Get-MarkerField $c26MarkerLine 'postStackRootSourceCount') }; accounting=[ordered]@{ managedFrames=(Get-MarkerField $c26MarkerLine 'managedFrames'); nativeUnwinds=(Get-MarkerField $c26MarkerLine 'nativeUnwindCount'); thirdUnwinds=(Get-MarkerField $c26MarkerLine 'thirdUnwindAttempts'); boundsConsumed=(Get-MarkerField $c26MarkerLine 'stackBoundsConsumed') }; queue=[ordered]@{ beforeStack=(Get-MarkerField $c26MarkerLine 'queueCursorBeforeStack'); afterStack=(Get-MarkerField $c26MarkerLine 'queueCursorAfterStack'); atGcScanRootsReturn=(Get-MarkerField $c26MarkerLine 'queueCursorAtGcScanRootsReturn'); firstPostScanQueueOperation=(Get-MarkerField $c26MarkerLine 'firstPostScanQueueOperation') };
                graph=[ordered]@{ markWrites=(Get-MarkerField $c26MarkerLine 'markWrites'); childReads=(Get-MarkerField $c26MarkerLine 'childReads'); traversal=(Get-MarkerField $c26MarkerLine 'graphTraversal') }
            }
        } elseif ($isC011EC25) {
            # validationText is whitespace-normalized above, so extract the
            # two C25 records by their unique marker spans.  A whole-log
            # Get-MarkerField lookup would otherwise select same-named C24
            # fields that precede this boundary.
            $c25PreflightStart = $validationText.IndexOf(
                '[nativeaot-gc-native-entry-boundary] preflight marker=C011EC25-PREFLIGHT')
            $c25StopStart = $validationText.IndexOf(
                '[nativeaot-gc-native-caller-provenance] SAFE_STOP')
            $c25MarkerLine = if ($c25StopStart -ge 0) {
                $validationText.Substring($c25StopStart)
            } else { $null }
            $c25PreflightLine = if ($c25PreflightStart -ge 0 -and
                $c25StopStart -gt $c25PreflightStart) {
                $validationText.Substring(
                    $c25PreflightStart, $c25StopStart - $c25PreflightStart)
            } else { $null }
            if ([string]::IsNullOrWhiteSpace($c25PreflightLine) -or
                [string]::IsNullOrWhiteSpace($c25MarkerLine)) {
                throw "C011EC25 preflight/safe-stop evidence was incomplete in $name."
            }
            foreach ($field in @(
                'secondMetadataValid','secondOutputAgreement','secondOpcodeCount',
                'secondStackAdvance','secondInputRIP','secondInputRSP','secondInputRBP',
                'secondReturnSlot','secondReturnValue','expectedCallerRIP',
                'expectedCallerRSP','secondOutputRIP','secondOutputRSP','secondOutputRBP',
                'secondEstablisherFrame','secondHandlerData','thirdPhysicalPC',
                'thirdLinkedPC','linkedEntryPC','linkedHaltPC','bootStackTop',
                'thirdInKernelRange','thirdLinkedLookupAttempted',
                'thirdLinkedLookupSucceeded','thirdPhysicalLookupAttempted',
                'thirdPhysicalLookupSucceeded','thirdMetadataPresent',
                'assemblyEntryBoundary','nonReturningHandoff','stackBottomProven',
                'providerLookupResult','linkedLookupResult','physicalLookupResult')) {
                if ($null -eq (Get-MarkerField $c25PreflightLine $field)) {
                    throw "C011EC25 preflight field $field was missing in $name."
                }
            }
            foreach ($field in @(
                'c25PreflightProven','c25SecondMetadataValid',
                'c25SecondOutputAgreement','c25ThirdInKernelRange',
                'c25ThirdLinkedLookupAttempted','c25ThirdLinkedLookupSucceeded',
                'c25ThirdPhysicalLookupAttempted','c25ThirdPhysicalLookupSucceeded',
                'c25ThirdMetadataPresent','c25AssemblyEntryBoundary',
                'c25NonReturningHandoff','c25StackBottomProven',
                'c25SecondOpcodeCount','c25SecondStackAdvance',
                'c25ProviderLookupResult','c25LinkedLookupResult',
                'c25PhysicalLookupResult','c25SafeStopReason',
                'c25SecondReturnSlot','c25SecondReturnValue',
                'c25ExpectedCallerRIP','c25ExpectedCallerRSP','c25ThirdPhysicalPC',
                'c25ThirdLinkedPC','c25LinkedEntryPC','c25LinkedHaltPC',
                'c25BootStackTop','c25SecondEstablisherFrame',
                'c25SecondHandlerData','c25SecondRecoveredRBX',
                'c25SecondRecoveredRSI','c25SecondRecoveredRDI',
                'c25SecondRecoveredRBP')) {
                if ($null -eq (Get-MarkerField $c25MarkerLine $field)) {
                    throw "C011EC25 field $field was missing in $name."
                }
            }
            foreach ($expected in @(
                @('preflightProven','0x00000001'),
                @('outputAgreement','0x00000001'),
                @('standaloneTests','0x00000002'),
                @('helperStandalonePassed','0x00000001'),
                @('secondStandalonePassed','0x00000001'),
                @('callerValid','0x00000001'),
                @('callerKernelRange','0x00000001'),
                @('callerManagedRange','0x00000000'),
                @('secondProviderLookupSucceeded','0x00000001'),
                @('secondProductionUnwindAttempted','0x00000001'),
                @('nativeFramesCrossed','0x00000002'),
                @('c19RootReports','0x00000004'),
                @('c19RegisterRoots','0x00000003'),
                @('c19StackRoots','0x00000001'),
                @('c19PromoteAttempts','0x00000004'),
                @('c19PromoteEntries','0x00000004'),
                @('c19PromoteReturns','0x00000004'),
                @('totalRoots','0x00000006'),
                @('framesWalked','0x00000001'),
                @('stackBoundsConsumed','0x00000000'),
                @('markWrites','0x00000000'),
                @('childReads','0x00000000'),
                @('graphTraversal','0x00000000'),
                @('safeStopReason','0x00000000'),
                @('c25PreflightProven','0x00000001'),
                @('c25SecondMetadataValid','0x00000001'),
                @('c25SecondOutputAgreement','0x00000001'),
                @('c25ThirdInKernelRange','0x00000001'),
                @('c25ThirdLinkedLookupAttempted','0x00000001'),
                @('c25ThirdLinkedLookupSucceeded','0x00000000'),
                @('c25ThirdPhysicalLookupAttempted','0x00000001'),
                @('c25ThirdPhysicalLookupSucceeded','0x00000000'),
                @('c25ThirdMetadataPresent','0x00000000'),
                @('c25AssemblyEntryBoundary','0x00000001'),
                @('c25NonReturningHandoff','0x00000001'),
                @('c25StackBottomProven','0x00000001'),
                @('c25SafeStopReason','0xC0250000'))) {
                if ((Get-MarkerField $c25MarkerLine $expected[0]) -ne $expected[1]) {
                    throw "C011EC25 expected $($expected[0])=$($expected[1]) in $name."
                }
            }
            foreach ($pair in @(
                @('secondReturnValue','secondOutputRIP'),
                @('expectedCallerRIP','secondOutputRIP'),
                @('expectedCallerRSP','secondOutputRSP'),
                @('secondReturnValue','c25SecondReturnValue'),
                @('secondReturnSlot','c25SecondReturnSlot'),
                @('secondOutputRIP','c25ThirdPhysicalPC'),
                @('thirdLinkedPC','c25ThirdLinkedPC'),
                @('linkedHaltPC','c25LinkedHaltPC'),
                @('bootStackTop','c25BootStackTop'))) {
                $left = Get-MarkerField $c25PreflightLine $pair[0]
                $right = Get-MarkerField $c25MarkerLine $pair[1]
                if ($pair[1] -in @('c25ThirdPhysicalPC','c25ThirdLinkedPC','c25LinkedHaltPC','c25BootStackTop')) {
                    $right = Get-MarkerField $c25MarkerLine $pair[1]
                } elseif ($right -eq $null) {
                    $right = Get-MarkerField $c25PreflightLine $pair[1]
                }
                if ($left -ne $right) { throw "C011EC25 provenance mismatch: $($pair[0]) != $($pair[1]) in $name." }
            }
            if ((Get-MarkerField $c25PreflightLine 'secondOpcodeCount') -eq '0x00000000' -or
                (Get-MarkerField $c25PreflightLine 'secondStackAdvance') -eq '0x00000000') {
                throw "C011EC25 did not decode a non-empty kernel_main unwind program in $name."
            }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath);
                safeStopMarker='C011EC25'; outcome='B'; harnessTerminated=$true;
                markerLine=$c25MarkerLine; preflightLine=$c25PreflightLine;
                unwind=[ordered]@{
                    secondMetadataValid=(Get-MarkerField $c25MarkerLine 'c25SecondMetadataValid');
                    secondOutputAgreement=(Get-MarkerField $c25MarkerLine 'c25SecondOutputAgreement');
                    secondOpcodeCount=(Get-MarkerField $c25MarkerLine 'c25SecondOpcodeCount');
                    secondStackAdvance=(Get-MarkerField $c25MarkerLine 'c25SecondStackAdvance');
                    secondInputRIP=(Get-MarkerField $c25PreflightLine 'secondInputRIP');
                    secondInputRSP=(Get-MarkerField $c25PreflightLine 'secondInputRSP');
                    secondInputRBP=(Get-MarkerField $c25PreflightLine 'secondInputRBP');
                    secondReturnSlot=(Get-MarkerField $c25PreflightLine 'secondReturnSlot');
                    secondReturnValue=(Get-MarkerField $c25PreflightLine 'secondReturnValue');
                    expectedCallerRIP=(Get-MarkerField $c25PreflightLine 'expectedCallerRIP');
                    expectedCallerRSP=(Get-MarkerField $c25PreflightLine 'expectedCallerRSP');
                    secondOutputRIP=(Get-MarkerField $c25PreflightLine 'secondOutputRIP');
                    secondOutputRSP=(Get-MarkerField $c25PreflightLine 'secondOutputRSP');
                    secondOutputRBP=(Get-MarkerField $c25PreflightLine 'secondOutputRBP');
                    secondEstablisherFrame=(Get-MarkerField $c25PreflightLine 'secondEstablisherFrame');
                    secondHandlerData=(Get-MarkerField $c25PreflightLine 'secondHandlerData')
                };
                boundary=[ordered]@{
                    thirdPhysicalPC=(Get-MarkerField $c25PreflightLine 'thirdPhysicalPC');
                    thirdLinkedPC=(Get-MarkerField $c25PreflightLine 'thirdLinkedPC');
                    linkedEntryPC=(Get-MarkerField $c25PreflightLine 'linkedEntryPC');
                    linkedHaltPC=(Get-MarkerField $c25PreflightLine 'linkedHaltPC');
                    bootStackTop=(Get-MarkerField $c25PreflightLine 'bootStackTop');
                    thirdInKernelRange=(Get-MarkerField $c25MarkerLine 'c25ThirdInKernelRange');
                    assemblyEntryBoundary=(Get-MarkerField $c25MarkerLine 'c25AssemblyEntryBoundary');
                    nonReturningHandoff=(Get-MarkerField $c25MarkerLine 'c25NonReturningHandoff');
                    stackBottomProven=(Get-MarkerField $c25MarkerLine 'c25StackBottomProven');
                    thirdMetadataPresent=(Get-MarkerField $c25MarkerLine 'c25ThirdMetadataPresent');
                    linkedLookupResult=(Get-MarkerField $c25MarkerLine 'c25LinkedLookupResult');
                    physicalLookupResult=(Get-MarkerField $c25MarkerLine 'c25PhysicalLookupResult')
                };
                caller=[ordered]@{ valid=(Get-MarkerField $c25MarkerLine 'callerValid'); kernelRange=(Get-MarkerField $c25MarkerLine 'callerKernelRange'); managedRange=(Get-MarkerField $c25MarkerLine 'callerManagedRange') };
                roots=[ordered]@{ total=(Get-MarkerField $c25MarkerLine 'totalRoots'); category3=(Get-MarkerField $c25MarkerLine 'c19RootReports'); register=(Get-MarkerField $c25MarkerLine 'c19RegisterRoots'); stack=(Get-MarkerField $c25MarkerLine 'c19StackRoots'); promoteAttempts=(Get-MarkerField $c25MarkerLine 'c19PromoteAttempts'); promoteEntries=(Get-MarkerField $c25MarkerLine 'c19PromoteEntries'); promoteReturns=(Get-MarkerField $c25MarkerLine 'c19PromoteReturns') };
                accounting=[ordered]@{ frames=(Get-MarkerField $c25MarkerLine 'framesWalked'); total=(Get-MarkerField $c25MarkerLine 'totalRoots'); stackBoundsConsumed=(Get-MarkerField $c25MarkerLine 'stackBoundsConsumed'); markWrites=(Get-MarkerField $c25MarkerLine 'markWrites'); childReads=(Get-MarkerField $c25MarkerLine 'childReads'); graphTraversal=(Get-MarkerField $c25MarkerLine 'graphTraversal') }
            }
        } elseif ($isC011EC24) {
            $c24MarkerLine = (($validationText -split "`n") | Where-Object {
                $_ -match '\[nativeaot-gc-native-caller-provenance\] SAFE_STOP' -and
                $_ -match 'marker=C011EC24(\s|$)'
            } | Select-Object -Last 1)
            $c24PreflightLine = (($validationText -split "`n") | Where-Object { $_ -match 'marker=C011EC24-PREFLIGHT' } | Select-Object -Last 1)
            if ([string]::IsNullOrWhiteSpace($c24PreflightLine) -or [string]::IsNullOrWhiteSpace($c24MarkerLine)) {
                throw "C011EC24 preflight/safe-stop evidence was incomplete in $name."
            }
            $c24Fields = @('preflightProven','outputAgreement','callerValid','callerKernelRange','callerManagedRange','standaloneTests','helperStandalonePassed','secondStandalonePassed','unwindOpcodeCount','stackAdvance','lookupAttempts','lookupSuccesses','unwindAttempts','rtlVirtualUnwindCalls','rtlVirtualUnwindReturned','unwindResult','nativeFramesCrossed','managedReentry','secondProviderLookupAttempted','secondProviderLookupSucceeded','secondProductionUnwindAttempted','secondUnwindResult','outcome','inputRIP','inputRSP','inputRBP','returnSlot','returnValue','expectedCallerRIP','expectedCallerRSP','outputRIP','outputRSP','outputRBP','secondModuleBase','secondExecutableStart','secondExecutableEnd','secondRuntimeFunction','secondUnwindInfo','secondInputRIP','secondInputRSP','secondInputRBP','secondOutputRIP','secondOutputRSP','secondOutputRBP','preflightReturnSlot','preflightReturnValue','preflightOutputRIP','preflightOutputRSP','liveRSP','sourceRBX','sourceRSI','sourceRDI','sourceRBP','sourceR12','sourceR13','sourceR14','sourceR15','preRBX','preRSI','preRDI','preRBP','preR12','preR13','preR14','preR15','recoveredRBX','recoveredRSI','recoveredRDI','recoveredRBP','recoveredR12','recoveredR13','recoveredR14','recoveredR15','c19RootReports','c19RegisterRoots','c19StackRoots','c19PromoteAttempts','c19PromoteEntries','c19PromoteReturns','framesWalked','totalRoots','stackBoundsConsumed','markWrites','childReads','graphTraversal','queueCursorBefore','queueCursorAfter','safeStopReason')
            foreach ($field in $c24Fields) { if ($null -eq (Get-MarkerField $c24MarkerLine $field)) { throw "C011EC24 field $field was missing in $name." } }
            foreach ($expected in @(@('preflightProven','0x00000001'),@('outputAgreement','0x00000001'),@('standaloneTests','0x00000002'),@('helperStandalonePassed','0x00000001'),@('secondStandalonePassed','0x00000001'),@('unwindOpcodeCount','0x0000000A'),@('stackAdvance','0x000003B8'),@('callerValid','0x00000001'),@('callerKernelRange','0x00000001'),@('callerManagedRange','0x00000000'),@('secondProviderLookupAttempted','0x00000001'),@('secondProviderLookupSucceeded','0x00000001'),@('secondProductionUnwindAttempted','0x00000001'),@('secondUnwindResult','0x00000003'),@('lookupAttempts','0x00000002'),@('lookupSuccesses','0x00000002'),@('unwindAttempts','0x00000002'),@('rtlVirtualUnwindCalls','0x00000002'),@('rtlVirtualUnwindReturned','0x00000002'),@('unwindResult','0x00000003'),@('nativeFramesCrossed','0x00000002'),@('totalRoots','0x00000006'),@('c19RootReports','0x00000004'),@('c19RegisterRoots','0x00000003'),@('c19StackRoots','0x00000001'),@('c19PromoteAttempts','0x00000004'),@('c19PromoteEntries','0x00000004'),@('c19PromoteReturns','0x00000004'),@('stackBoundsConsumed','0x00000000'),@('markWrites','0x00000000'),@('childReads','0x00000000'),@('graphTraversal','0x00000000'),@('safeStopReason','0xC0240004'))) { if ((Get-MarkerField $c24MarkerLine $expected[0]) -ne $expected[1]) { throw "C011EC24 expected $($expected[0])=$($expected[1]) in $name." } }
            foreach ($pair in @(@('returnValue','outputRIP'),@('expectedCallerRIP','outputRIP'),@('expectedCallerRSP','outputRSP'),@('preflightReturnValue','returnValue'),@('preflightReturnSlot','returnSlot'),@('preflightOutputRIP','outputRIP'),@('preflightOutputRSP','outputRSP'))) { if ((Get-MarkerField $c24MarkerLine $pair[0]) -ne (Get-MarkerField $c24MarkerLine $pair[1])) { throw "C011EC24 provenance mismatch: $($pair[0]) != $($pair[1]) in $name." } }
            if ((Get-MarkerField $c24MarkerLine 'secondOutputRIP') -eq (Get-MarkerField $c24MarkerLine 'outputRIP') -or (Get-MarkerField $c24MarkerLine 'secondOutputRSP') -le (Get-MarkerField $c24MarkerLine 'outputRSP')) { throw "C011EC24 second unwind did not produce a distinct caller in $name." }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker='C011EC24'; outcome='C'; harnessTerminated=$true; markerLine=$c24MarkerLine; preflightLine=$c24PreflightLine
                unwind=[ordered]@{ lookupAttempts=(Get-MarkerField $c24MarkerLine 'lookupAttempts'); lookupSuccesses=(Get-MarkerField $c24MarkerLine 'lookupSuccesses'); attempts=(Get-MarkerField $c24MarkerLine 'unwindAttempts'); rtlVirtualUnwindCalls=(Get-MarkerField $c24MarkerLine 'rtlVirtualUnwindCalls'); rtlVirtualUnwindReturned=(Get-MarkerField $c24MarkerLine 'rtlVirtualUnwindReturned'); result=(Get-MarkerField $c24MarkerLine 'unwindResult'); inputRIP=(Get-MarkerField $c24MarkerLine 'inputRIP'); inputRSP=(Get-MarkerField $c24MarkerLine 'inputRSP'); inputRBP=(Get-MarkerField $c24MarkerLine 'inputRBP'); outputRIP=(Get-MarkerField $c24MarkerLine 'outputRIP'); outputRSP=(Get-MarkerField $c24MarkerLine 'outputRSP'); derivedReturnSlot=(Get-MarkerField $c24MarkerLine 'returnSlot'); derivedReturnValue=(Get-MarkerField $c24MarkerLine 'returnValue'); expectedCallerRSP=(Get-MarkerField $c24MarkerLine 'expectedCallerRSP'); opcodeCount=(Get-MarkerField $c24MarkerLine 'unwindOpcodeCount'); stackAdvance=(Get-MarkerField $c24MarkerLine 'stackAdvance'); secondProviderLookupAttempted=(Get-MarkerField $c24MarkerLine 'secondProviderLookupAttempted'); secondProviderLookupSucceeded=(Get-MarkerField $c24MarkerLine 'secondProviderLookupSucceeded'); secondProductionUnwindAttempted=(Get-MarkerField $c24MarkerLine 'secondProductionUnwindAttempted'); secondUnwindResult=(Get-MarkerField $c24MarkerLine 'secondUnwindResult'); secondModuleBase=(Get-MarkerField $c24MarkerLine 'secondModuleBase'); secondExecutableStart=(Get-MarkerField $c24MarkerLine 'secondExecutableStart'); secondExecutableEnd=(Get-MarkerField $c24MarkerLine 'secondExecutableEnd'); secondRuntimeFunction=(Get-MarkerField $c24MarkerLine 'secondRuntimeFunction'); secondUnwindInfo=(Get-MarkerField $c24MarkerLine 'secondUnwindInfo'); secondInputRip=(Get-MarkerField $c24MarkerLine 'secondInputRIP'); secondInputRsp=(Get-MarkerField $c24MarkerLine 'secondInputRSP'); secondInputRbp=(Get-MarkerField $c24MarkerLine 'secondInputRBP'); secondOutputRip=(Get-MarkerField $c24MarkerLine 'secondOutputRIP'); secondOutputRsp=(Get-MarkerField $c24MarkerLine 'secondOutputRSP'); secondOutputRbp=(Get-MarkerField $c24MarkerLine 'secondOutputRBP') }
                caller=[ordered]@{ valid=(Get-MarkerField $c24MarkerLine 'callerValid'); kernelRange=(Get-MarkerField $c24MarkerLine 'callerKernelRange'); managedRange=(Get-MarkerField $c24MarkerLine 'callerManagedRange') }
                roots=[ordered]@{ total=(Get-MarkerField $c24MarkerLine 'totalRoots'); category3=(Get-MarkerField $c24MarkerLine 'c19RootReports'); register=(Get-MarkerField $c24MarkerLine 'c19RegisterRoots'); stack=(Get-MarkerField $c24MarkerLine 'c19StackRoots'); promoteAttempts=(Get-MarkerField $c24MarkerLine 'c19PromoteAttempts'); promoteEntries=(Get-MarkerField $c24MarkerLine 'c19PromoteEntries'); promoteReturns=(Get-MarkerField $c24MarkerLine 'c19PromoteReturns') }
                accounting=[ordered]@{ frames=(Get-MarkerField $c24MarkerLine 'framesWalked'); total=(Get-MarkerField $c24MarkerLine 'totalRoots'); stackBoundsConsumed=(Get-MarkerField $c24MarkerLine 'stackBoundsConsumed'); markWrites=(Get-MarkerField $c24MarkerLine 'markWrites'); childReads=(Get-MarkerField $c24MarkerLine 'childReads'); graphTraversal=(Get-MarkerField $c24MarkerLine 'graphTraversal') }
            }
        } elseif ($isC011EC23) {
            Assert-Text $validationText 'marker=C011EC23(\s|$)' "C011EC23 native-unwind marker"
            $c23PreflightLine = (($validationText -split "`n") | Where-Object { $_ -match 'marker=C011EC23-PREFLIGHT' } | Select-Object -Last 1)
            if ([string]::IsNullOrWhiteSpace($c23PreflightLine)) { throw "C011EC23-PREFLIGHT was not emitted in $name." }
            foreach ($field in @('helperPC','moduleBase','pdataStart','pdataEnd','runtimeFunction','unwindInfo','entries')) {
                if ((Get-MarkerField $c23PreflightLine $field) -eq $null) { throw "C011EC23-PREFLIGHT field $field was missing in $name." }
            }
            if ((Get-MarkerField $c23PreflightLine 'runtimeFunction') -eq '0x0000000000000000' -or
                (Get-MarkerField $c23PreflightLine 'unwindInfo') -eq '0x0000000000000000' -or
                (Get-MarkerField $c23PreflightLine 'entries') -eq '0x00000000') {
                throw "C011EC23-PREFLIGHT did not prove registered lookup coverage in $name."
            }
            $c23Line = (($validationText -split "`n") | Where-Object {
                $_ -match '\[nativeaot-gc-native-unwind\] SAFE_STOP' -and
                $_ -match 'marker=C011EC23(\s|$)'
            } | Select-Object -Last 1)
            if ([string]::IsNullOrWhiteSpace($c23Line)) { throw "C011EC23 marker line was not isolated in $name." }
            foreach ($field in @(
                'lookupAttempts','lookupSuccesses','unwindAttempts','rtlVirtualUnwindCalls',
                'rtlVirtualUnwindReturned','unwindResult','nativeFramesCrossed','managedReentry',
                'callerManagedRange','callerCodeManagerFound','callerFindMethodInfoAttempts',
                'callerFindMethodInfoSuccess','inputRIP','inputRSP','inputRBP','outputRIP',
                'outputRSP','outputRBP','establisherFrame','moduleBase','pdataStart','pdataEnd',
                'xdataStart','xdataEnd','runtimeFunction','unwindInfo','beginRVA','endRVA',
                'unwindRVA','unwindVersion','unwindFlags','prologueSize','unwindCodeCount',
                'frameRegister','frameOffset','restoredRegisterCount','framesWalked','totalRoots',
                'secondFunctionAttempted','secondFunctionSucceeded','secondFunctionResult',
                'secondFunctionIndex','secondRuntimeFunction','secondUnwindInfo',
                'secondOutputRIP','secondOutputRSP',
                'c19RootReports','c19RegisterRoots','c19StackRoots','c19PromoteAttempts',
                'c19PromoteEntries','c19PromoteReturns','stackBoundsConsumed','markWrites',
                'childReads','graphTraversal','promoteEntries','promoteReturns','safeStopReason',
                'outcome')) {
                if ((Get-MarkerField $c23Line $field) -eq $null) { throw "C011EC23 field $field was missing in $name." }
            }
            $lookupSuccesses = [Convert]::ToUInt32((Get-MarkerField $c23Line 'lookupSuccesses').Substring(2), 16)
            $unwindAttempts = [Convert]::ToUInt32((Get-MarkerField $c23Line 'unwindAttempts').Substring(2), 16)
            $framesCrossed = [Convert]::ToUInt32((Get-MarkerField $c23Line 'nativeFramesCrossed').Substring(2), 16)
            $inputRsp = [Convert]::ToUInt64((Get-MarkerField $c23Line 'inputRSP').Substring(2), 16)
            $outputRsp = [Convert]::ToUInt64((Get-MarkerField $c23Line 'outputRSP').Substring(2), 16)
            $inputRip = Get-MarkerField $c23Line 'inputRIP'
            $outputRip = Get-MarkerField $c23Line 'outputRIP'
            if ($lookupSuccesses -eq 0 -or $unwindAttempts -eq 0 -or $framesCrossed -eq 0 -or
                (Get-MarkerField $c23Line 'secondFunctionAttempted') -ne '0x00000001' -or
                (Get-MarkerField $c23Line 'secondFunctionSucceeded') -ne '0x00000001' -or
                $inputRip -eq $outputRip -or $outputRsp -le $inputRsp -or
                (Get-MarkerField $c23Line 'rtlVirtualUnwindCalls') -ne '0x00000001' -or
                (Get-MarkerField $c23Line 'rtlVirtualUnwindReturned') -ne '0x00000001' -or
                (Get-MarkerField $c23Line 'unwindVersion') -ne '0x00000001' -or
                (Get-MarkerField $c23Line 'unwindFlags') -ne '0x00000000' -or
                (Get-MarkerField $c23Line 'totalRoots') -ne '0x00000006' -or
                (Get-MarkerField $c23Line 'c19RootReports') -ne '0x00000004' -or
                (Get-MarkerField $c23Line 'c19RegisterRoots') -ne '0x00000003' -or
                (Get-MarkerField $c23Line 'c19StackRoots') -ne '0x00000001' -or
                (Get-MarkerField $c23Line 'c19PromoteAttempts') -ne '0x00000004' -or
                (Get-MarkerField $c23Line 'c19PromoteEntries') -ne '0x00000004' -or
                (Get-MarkerField $c23Line 'c19PromoteReturns') -ne '0x00000004' -or
                (Get-MarkerField $c23Line 'stackBoundsConsumed') -ne '0x00000000' -or
                (Get-MarkerField $c23Line 'markWrites') -ne '0x00000000' -or
                (Get-MarkerField $c23Line 'childReads') -ne '0x00000000' -or
                (Get-MarkerField $c23Line 'graphTraversal') -ne '0x00000000') {
                throw "C011EC23 did not prove bounded genuine native unwind or preserve the C011EC19 chronology in $name."
            }
            $runOutcome = if ((Get-MarkerField $c23Line 'managedReentry') -eq '0x00000001') { 'B' } else { 'C' }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker='C011EC23'; outcome=$runOutcome; harnessTerminated=$true; markerLine=$c23Line
                unwind=[ordered]@{ lookupAttempts=(Get-MarkerField $c23Line 'lookupAttempts'); lookupSuccesses=(Get-MarkerField $c23Line 'lookupSuccesses'); attempts=(Get-MarkerField $c23Line 'unwindAttempts'); rtlVirtualUnwindCalls=(Get-MarkerField $c23Line 'rtlVirtualUnwindCalls'); rtlVirtualUnwindReturned=(Get-MarkerField $c23Line 'rtlVirtualUnwindReturned'); result=(Get-MarkerField $c23Line 'unwindResult'); inputRIP=$inputRip; inputRSP=(Get-MarkerField $c23Line 'inputRSP'); inputRBP=(Get-MarkerField $c23Line 'inputRBP'); outputRIP=$outputRip; outputRSP=(Get-MarkerField $c23Line 'outputRSP'); outputRBP=(Get-MarkerField $c23Line 'outputRBP'); establisherFrame=(Get-MarkerField $c23Line 'establisherFrame'); moduleBase=(Get-MarkerField $c23Line 'moduleBase'); pdataStart=(Get-MarkerField $c23Line 'pdataStart'); pdataEnd=(Get-MarkerField $c23Line 'pdataEnd'); xdataStart=(Get-MarkerField $c23Line 'xdataStart'); xdataEnd=(Get-MarkerField $c23Line 'xdataEnd'); runtimeFunction=(Get-MarkerField $c23Line 'runtimeFunction'); unwindInfo=(Get-MarkerField $c23Line 'unwindInfo'); beginRVA=(Get-MarkerField $c23Line 'beginRVA'); endRVA=(Get-MarkerField $c23Line 'endRVA'); unwindRVA=(Get-MarkerField $c23Line 'unwindRVA'); unwindVersion=(Get-MarkerField $c23Line 'unwindVersion'); unwindFlags=(Get-MarkerField $c23Line 'unwindFlags'); prologueSize=(Get-MarkerField $c23Line 'prologueSize'); unwindCodeCount=(Get-MarkerField $c23Line 'unwindCodeCount'); frameRegister=(Get-MarkerField $c23Line 'frameRegister'); frameOffset=(Get-MarkerField $c23Line 'frameOffset'); restoredRegisterCount=(Get-MarkerField $c23Line 'restoredRegisterCount'); secondFunctionAttempted=(Get-MarkerField $c23Line 'secondFunctionAttempted'); secondFunctionSucceeded=(Get-MarkerField $c23Line 'secondFunctionSucceeded'); secondFunctionResult=(Get-MarkerField $c23Line 'secondFunctionResult'); secondFunctionIndex=(Get-MarkerField $c23Line 'secondFunctionIndex'); secondRuntimeFunction=(Get-MarkerField $c23Line 'secondRuntimeFunction'); secondUnwindInfo=(Get-MarkerField $c23Line 'secondUnwindInfo'); secondOutputRIP=(Get-MarkerField $c23Line 'secondOutputRIP'); secondOutputRSP=(Get-MarkerField $c23Line 'secondOutputRSP') }
                caller=[ordered]@{ managed=(Get-MarkerField $c23Line 'managedReentry'); managedRange=(Get-MarkerField $c23Line 'callerManagedRange'); codeManager=(Get-MarkerField $c23Line 'callerCodeManager'); findMethodInfoAttempts=(Get-MarkerField $c23Line 'callerFindMethodInfoAttempts'); findMethodInfoSuccess=(Get-MarkerField $c23Line 'callerFindMethodInfoSuccess') }
                roots=[ordered]@{ total=(Get-MarkerField $c23Line 'totalRoots'); category3=(Get-MarkerField $c23Line 'c19RootReports'); register=(Get-MarkerField $c23Line 'c19RegisterRoots'); stack=(Get-MarkerField $c23Line 'c19StackRoots'); promoteAttempts=(Get-MarkerField $c23Line 'c19PromoteAttempts'); promoteEntries=(Get-MarkerField $c23Line 'c19PromoteEntries'); promoteReturns=(Get-MarkerField $c23Line 'c19PromoteReturns') }
                accounting=[ordered]@{ frames=(Get-MarkerField $c23Line 'framesWalked'); totalRoots=(Get-MarkerField $c23Line 'totalRoots'); stackBoundsConsumed=(Get-MarkerField $c23Line 'stackBoundsConsumed'); markWrites=(Get-MarkerField $c23Line 'markWrites'); childReads=(Get-MarkerField $c23Line 'childReads'); graphTraversal=(Get-MarkerField $c23Line 'graphTraversal'); promoteEntries=(Get-MarkerField $c23Line 'promoteEntries'); promoteReturns=(Get-MarkerField $c23Line 'promoteReturns') }
            }
        } elseif ($isC011EC21 -and -not $isC011EC23) {
            Assert-Text $validationText 'marker=C011EC21(\s|$)' "C011EC21 native continuation marker"
            $c21Line = ($validationText -replace '\r?\n', ' ')
            foreach ($field in @(
                'transitionFrameType','transitionFrame','transitionSavedRIP','transitionSavedSP','transitionSavedFP',
                'previousTransitionFrame','crossingAttempts','crossingResults','unwindAttempts',
                'rtlVirtualUnwindCalls','unwindResult','rtlVirtualUnwindReturned','outputRIP','outputRSP','outputRBP',
                'callerManagedRange','callerCodeManagerFound','callerFindMethodInfoAttempts','callerGcInfoAttempted',
                'framesWalked','totalRoots','c19RootReports','c19RegisterRoots','c19StackRoots',
                'c19PromoteAttempts','c19PromoteEntries','c19PromoteReturns','stackBoundsConsumed',
                'markWrites','childReads','graphTraversal','promoteEntries','promoteReturns',
                'c21NativeFrameCandidate','c21NativeUnwindAttempts','c21NativeUnwindMetadata','c21NativeUnwindResult',
                'c21ManagedReentry','c21ManagedStackBottom','c21NullPredecessorMeaning','c21TransitionLinkingDefect',
                'c21Outcome','c21NativeRIP','c21NativeRSP','c21NativeRBP','c21HelperStart','c21FunctionOffset',
                'c21CallSite','c21ModuleIdentity','c21SectionIdentity','c21RuntimeFunction','c21UnwindInfo',
                'c19SecondQueueInsertions','c19SecondQueueSlot','c19SecondQueueCursorBefore',
                'c19SecondQueueCursorAfter','c19SecondQueueOld','c19SecondQueueNew')) {
                if ((Get-MarkerField $c21Line $field) -eq $null) { throw "C011EC21 field $field was missing in $name." }
            }
            if ((Get-MarkerField $c21Line 'transitionFrameType') -ne '0x0000000000000001' -or
                (Get-MarkerField $c21Line 'previousTransitionFrame') -ne '0x0000000000000000' -or
                (Get-MarkerField $c21Line 'crossingAttempts') -ne '0x00000001' -or
                (Get-MarkerField $c21Line 'crossingResults') -ne '0x00000001' -or
                (Get-MarkerField $c21Line 'unwindAttempts') -ne '0x00000001' -or
                (Get-MarkerField $c21Line 'rtlVirtualUnwindCalls') -ne '0x00000001' -or
                (Get-MarkerField $c21Line 'unwindResult') -ne '0x00000001' -or
                (Get-MarkerField $c21Line 'rtlVirtualUnwindReturned') -ne '0x00000001' -or
                (Get-MarkerField $c21Line 'callerManagedRange') -ne '0x00000000' -or
                (Get-MarkerField $c21Line 'callerCodeManagerFound') -ne '0x00000000' -or
                (Get-MarkerField $c21Line 'callerFindMethodInfoAttempts') -ne '0x00000000' -or
                (Get-MarkerField $c21Line 'callerGcInfoAttempted') -ne '0x00000000' -or
                (Get-MarkerField $c21Line 'framesWalked') -ne '0x00000001' -or
                (Get-MarkerField $c21Line 'totalRoots') -ne '0x00000006' -or
                (Get-MarkerField $c21Line 'c19RootReports') -ne '0x00000004' -or
                (Get-MarkerField $c21Line 'c19RegisterRoots') -ne '0x00000003' -or
                (Get-MarkerField $c21Line 'c19StackRoots') -ne '0x00000001' -or
                (Get-MarkerField $c21Line 'c19PromoteAttempts') -ne '0x00000004' -or
                (Get-MarkerField $c21Line 'c19PromoteEntries') -ne '0x00000004' -or
                (Get-MarkerField $c21Line 'c19PromoteReturns') -ne '0x00000004' -or
                (Get-MarkerField $c21Line 'stackBoundsConsumed') -ne '0x00000000' -or
                (Get-MarkerField $c21Line 'markWrites') -ne '0x00000000' -or
                (Get-MarkerField $c21Line 'childReads') -ne '0x00000000' -or
                (Get-MarkerField $c21Line 'graphTraversal') -ne '0x00000000' -or
                (Get-MarkerField $c21Line 'c21NativeFrameCandidate') -ne '0x00000001' -or
                (Get-MarkerField $c21Line 'c21NativeUnwindAttempts') -ne '0x00000000' -or
                (Get-MarkerField $c21Line 'c21NativeUnwindMetadata') -ne '0x00000000' -or
                (Get-MarkerField $c21Line 'c21NativeUnwindResult') -ne '0x00000000' -or
                (Get-MarkerField $c21Line 'c21ManagedReentry') -ne '0x00000000' -or
                (Get-MarkerField $c21Line 'c21ManagedStackBottom') -ne '0x00000000' -or
                (Get-MarkerField $c21Line 'c21NullPredecessorMeaning') -ne '0x00000002' -or
                (Get-MarkerField $c21Line 'c21TransitionLinkingDefect') -ne '0x00000000' -or
                (Get-MarkerField $c21Line 'c21Outcome') -ne '0x00000005' -or
                (Get-MarkerField $c21Line 'c21NativeRIP') -ne (Get-MarkerField $c21Line 'outputRIP') -or
                (Get-MarkerField $c21Line 'c21NativeRSP') -ne (Get-MarkerField $c21Line 'outputRSP') -or
                (Get-MarkerField $c21Line 'c21NativeRBP') -ne (Get-MarkerField $c21Line 'outputRBP') -or
                (Get-MarkerField $c21Line 'c21ModuleIdentity') -ne '0x0000000000000001' -or
                (Get-MarkerField $c21Line 'c21SectionIdentity') -ne '0x0000000000000001' -or
                (Get-MarkerField $c21Line 'c21RuntimeFunction') -ne '0x0000000000000000' -or
                (Get-MarkerField $c21Line 'c21UnwindInfo') -ne '0x0000000000000000' -or
                (Get-MarkerField $c21Line 'c19SecondQueueInsertions') -ne '0x00000004' -or
                (Get-MarkerField $c21Line 'c19SecondQueueCursorBefore') -ne '0x0000000000000004' -or
                (Get-MarkerField $c21Line 'c19SecondQueueCursorAfter') -ne '0x0000000000000005' -or
                (Get-MarkerField $c21Line 'c19SecondQueueOld') -ne '0x0000000000000000' -or
                (Get-MarkerField $c21Line 'c19SecondQueueNew') -eq '0x0000000000000000') {
                throw "C011EC21 did not preserve the C20 unwind, native-boundary, root chronology, or queue-cursor contract in $name."
            }
            $c21Rip = [Convert]::ToUInt64((Get-MarkerField $c21Line 'c21NativeRIP').Substring(2), 16)
            $c21CallSite = [Convert]::ToUInt64((Get-MarkerField $c21Line 'c21CallSite').Substring(2), 16)
            if ($c21CallSite -ne ($c21Rip - [uint64]7)) {
                throw "C011EC21 call-site provenance did not identify the audited return-site instruction in $name."
            }
            if ($null -eq $nativeHelperAudit -or
                (Get-MarkerField $c21Line 'c21HelperStart') -ne $nativeHelperAudit.linkedAddress) {
                throw "C011EC21 runtime helper provenance did not match the linked kernel symbol audit in $name."
            }
            $c21Outcome = 'E'
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker='C011EC21'; outcome=$c21Outcome; harnessTerminated=$true; markerLine=$c21Line
                transition=[ordered]@{ frameType='PInvokeTransitionFrame'; frame=(Get-MarkerField $c21Line 'transitionFrame'); reversePInvokeType='1'; savedRIP=(Get-MarkerField $c21Line 'transitionSavedRIP'); savedSP=(Get-MarkerField $c21Line 'transitionSavedSP'); savedFP=(Get-MarkerField $c21Line 'transitionSavedFP'); previousFrame=(Get-MarkerField $c21Line 'previousTransitionFrame'); nullPredecessorMeaning='2 / no older transition record; locked iterator proceeds to ordinary native unwind/classification'; crossingAttempts=(Get-MarkerField $c21Line 'crossingAttempts'); crossingResults=(Get-MarkerField $c21Line 'crossingResults') }
                unwind=[ordered]@{ attempts=(Get-MarkerField $c21Line 'unwindAttempts'); rtlVirtualUnwindCalls=(Get-MarkerField $c21Line 'rtlVirtualUnwindCalls'); rtlVirtualUnwindReturned=(Get-MarkerField $c21Line 'rtlVirtualUnwindReturned'); rtlVirtualUnwindResult=(Get-MarkerField $c21Line 'rtlVirtualUnwindResult'); runtimeFunction=(Get-MarkerField $c21Line 'runtimeFunction'); unwindInfo=(Get-MarkerField $c21Line 'unwindInfo'); unwindInfoSize=(Get-MarkerField $c21Line 'unwindInfoSize'); blockFlags=(Get-MarkerField $c21Line 'unwindBlockFlags'); inputRIP=(Get-MarkerField $c21Line 'inputRIP'); inputRSP=(Get-MarkerField $c21Line 'inputRSP'); inputRBP=(Get-MarkerField $c21Line 'inputRBP'); outputRIP=(Get-MarkerField $c21Line 'outputRIP'); outputRSP=(Get-MarkerField $c21Line 'outputRSP'); outputRBP=(Get-MarkerField $c21Line 'outputRBP'); establisherFrame=(Get-MarkerField $c21Line 'establisherFrame'); restoredRBX=(Get-MarkerField $c21Line 'restoredRBX'); restoredRSI=(Get-MarkerField $c21Line 'restoredRSI'); restoredRDI=(Get-MarkerField $c21Line 'restoredRDI'); restoredR12=(Get-MarkerField $c21Line 'restoredR12'); restoredR13=(Get-MarkerField $c21Line 'restoredR13'); restoredR14=(Get-MarkerField $c21Line 'restoredR14'); restoredR15=(Get-MarkerField $c21Line 'restoredR15'); restoredRegisterCount=(Get-MarkerField $c21Line 'restoredRegisterCount') }
                caller=[ordered]@{ symbol='kernel::nativeaot_pal_qemu_test::(anonymous namespace)::runFirstRealAllocationImpl'; functionOffset=(Get-MarkerField $c21Line 'c21FunctionOffset'); rip=(Get-MarkerField $c21Line 'c21NativeRIP'); rsp=(Get-MarkerField $c21Line 'c21NativeRSP'); rbp=(Get-MarkerField $c21Line 'c21NativeRBP'); callSite=(Get-MarkerField $c21Line 'c21CallSite'); module='kernel.elf'; section='.text'; moduleIdentity=(Get-MarkerField $c21Line 'c21ModuleIdentity'); sectionIdentity=(Get-MarkerField $c21Line 'c21SectionIdentity'); managedRange=(Get-MarkerField $c21Line 'callerManagedRange'); codeManager=(Get-MarkerField $c21Line 'callerCodeManager'); findMethodInfoAttempts=(Get-MarkerField $c21Line 'callerFindMethodInfoAttempts'); gcInfoAttempted=(Get-MarkerField $c21Line 'callerGcInfoAttempted') }
                nativeFrameChain=@([ordered]@{ index=0; rip=(Get-MarkerField $c21Line 'c21NativeRIP'); rsp=(Get-MarkerField $c21Line 'c21NativeRSP'); rbp=(Get-MarkerField $c21Line 'c21NativeRBP'); symbol='runFirstRealAllocationImpl'; runtimeFunction='none'; unwindInfo='none'; unwindAttempted=$false; callerRIP=$null; callerRSP=$null; unwindResult='not attempted; metadata blocker' })
                nativeContinuation=[ordered]@{ frameCandidate=(Get-MarkerField $c21Line 'c21NativeFrameCandidate'); unwindAttempts=(Get-MarkerField $c21Line 'c21NativeUnwindAttempts'); metadataAvailable=(Get-MarkerField $c21Line 'c21NativeUnwindMetadata'); unwindResult=(Get-MarkerField $c21Line 'c21NativeUnwindResult'); managedReentry=(Get-MarkerField $c21Line 'c21ManagedReentry'); managedStackBottom=(Get-MarkerField $c21Line 'c21ManagedStackBottom'); helperStart=(Get-MarkerField $c21Line 'c21HelperStart'); helperEnd=(Get-MarkerField $c21Line 'c21HelperEnd'); callSite=(Get-MarkerField $c21Line 'c21CallSite'); runtimeFunction=(Get-MarkerField $c21Line 'c21RuntimeFunction'); unwindInfo=(Get-MarkerField $c21Line 'c21UnwindInfo'); outcome=(Get-MarkerField $c21Line 'c21Outcome') }
                roots=[ordered]@{ currentFrame=(Get-MarkerField $c21Line 'c19RootReports'); register=(Get-MarkerField $c21Line 'c19RegisterRoots'); stack=(Get-MarkerField $c21Line 'c19StackRoots'); promoteAttempts=(Get-MarkerField $c21Line 'c19PromoteAttempts'); promoteEntries=(Get-MarkerField $c21Line 'c19PromoteEntries'); promoteReturns=(Get-MarkerField $c21Line 'c19PromoteReturns') }
                accounting=[ordered]@{ frames=(Get-MarkerField $c21Line 'framesWalked'); callbacks=(Get-MarkerField $c21Line 'stackProviderCallbacks'); totalRoots=(Get-MarkerField $c21Line 'totalRoots'); markWrites=(Get-MarkerField $c21Line 'markWrites'); childReads=(Get-MarkerField $c21Line 'childReads'); graphTraversal=(Get-MarkerField $c21Line 'graphTraversal'); boundsConsumed=(Get-MarkerField $c21Line 'stackBoundsConsumed'); promoteEntries=(Get-MarkerField $c21Line 'promoteEntries'); promoteReturns=(Get-MarkerField $c21Line 'promoteReturns') }
                queue=[ordered]@{ insertions=(Get-MarkerField $c21Line 'c19SecondQueueInsertions'); slot=(Get-MarkerField $c21Line 'c19SecondQueueSlot'); cursorBefore=(Get-MarkerField $c21Line 'c19SecondQueueCursorBefore'); cursorAfter=(Get-MarkerField $c21Line 'c19SecondQueueCursorAfter'); old=(Get-MarkerField $c21Line 'c19SecondQueueOld'); new=(Get-MarkerField $c21Line 'c19SecondQueueNew') }
            }
        } elseif ($isC011EC20) {
            $c20ProofLine = (($validationText -split "`n") | Where-Object { $_ -match 'marker=C011EC20($|\s)' } | Select-Object -Last 1)
            $c20SafeStopLine = (($validationText -split "`n") | Where-Object { $_ -match 'marker=C011EC20-SAFE_STOP' } | Select-Object -Last 1)
            if ([string]::IsNullOrWhiteSpace($c20ProofLine) -and [string]::IsNullOrWhiteSpace($c20SafeStopLine)) { throw "C011EC20 did not emit a proof or classified safe-stop line in $name." }
            $c20Line = if (-not [string]::IsNullOrWhiteSpace($c20ProofLine)) { $c20ProofLine } else { $c20SafeStopLine }
            $c20OutcomeCode = Get-MarkerField $c20Line 'outcome'
            $c20Outcome = switch ($c20OutcomeCode) { '0x00000001' { 'A' } '0x00000002' { 'B' } '0x00000003' { 'C' } '0x00000005' { 'E' } default { 'D' } }
            if (-not [string]::IsNullOrWhiteSpace($c20ProofLine)) {
                foreach ($field in @('crossingAttempts','crossingResults','unwindAttempts','rtlVirtualUnwindCalls','unwindResult','rtlVirtualUnwindReturned','outputRIP','outputRSP','callerSpMoved','callerFrameDistinct','restoredRegisterCount')) {
                    if ((Get-MarkerField $c20Line $field) -eq $null -or (Get-MarkerField $c20Line $field) -eq '0x00000000') { throw "C011EC20 proof field $field was empty in $name." }
                }
                if ($c20Outcome -notin @('A','B','E')) { throw "C011EC20 emitted a proof marker without a valid A/B/E outcome in $name." }
            }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker=if ($c20Outcome -in @('A','B','E')) { 'C011EC20' } else { 'C011EC20-SAFE_STOP' }; outcome=$c20Outcome; harnessTerminated=$true; markerLine=$c20Line
                transition=[ordered]@{ frameType=(Get-MarkerField $c20Line 'transitionFrameType'); frame=(Get-MarkerField $c20Line 'transitionFrame'); savedRIP=(Get-MarkerField $c20Line 'transitionSavedRIP'); savedSP=(Get-MarkerField $c20Line 'transitionSavedSP'); savedFP=(Get-MarkerField $c20Line 'transitionSavedFP'); previousFrame=(Get-MarkerField $c20Line 'previousTransitionFrame'); crossingAttempts=(Get-MarkerField $c20Line 'crossingAttempts'); crossingResults=(Get-MarkerField $c20Line 'crossingResults') }
                unwind=[ordered]@{ attempts=(Get-MarkerField $c20Line 'unwindAttempts'); rtlVirtualUnwindCalls=(Get-MarkerField $c20Line 'rtlVirtualUnwindCalls'); rtlVirtualUnwindReturned=(Get-MarkerField $c20Line 'rtlVirtualUnwindReturned'); rtlVirtualUnwindResult=(Get-MarkerField $c20Line 'rtlVirtualUnwindResult'); runtimeFunction=(Get-MarkerField $c20Line 'runtimeFunction'); unwindInfo=(Get-MarkerField $c20Line 'unwindInfo'); unwindInfoSize=(Get-MarkerField $c20Line 'unwindInfoSize'); blockFlags=(Get-MarkerField $c20Line 'unwindBlockFlags'); inputRIP=(Get-MarkerField $c20Line 'inputRIP'); inputRSP=(Get-MarkerField $c20Line 'inputRSP'); inputRBP=(Get-MarkerField $c20Line 'inputRBP'); outputRIP=(Get-MarkerField $c20Line 'outputRIP'); outputRSP=(Get-MarkerField $c20Line 'outputRSP'); outputRBP=(Get-MarkerField $c20Line 'outputRBP'); establisherFrame=(Get-MarkerField $c20Line 'establisherFrame'); handlerData=(Get-MarkerField $c20Line 'handlerData'); restoredRBX=(Get-MarkerField $c20Line 'restoredRBX'); restoredRSI=(Get-MarkerField $c20Line 'restoredRSI'); restoredRDI=(Get-MarkerField $c20Line 'restoredRDI'); restoredR12=(Get-MarkerField $c20Line 'restoredR12'); restoredR13=(Get-MarkerField $c20Line 'restoredR13'); restoredR14=(Get-MarkerField $c20Line 'restoredR14'); restoredR15=(Get-MarkerField $c20Line 'restoredR15'); restoredRegisterCount=(Get-MarkerField $c20Line 'restoredRegisterCount') }
                caller=[ordered]@{ managedRange=(Get-MarkerField $c20Line 'callerManagedRange'); codeManager=(Get-MarkerField $c20Line 'callerCodeManager'); findMethodInfoAttempts=(Get-MarkerField $c20Line 'callerFindMethodInfoAttempts'); findMethodInfoSuccess=(Get-MarkerField $c20Line 'callerFindMethodInfoSuccess'); methodInfo=(Get-MarkerField $c20Line 'callerMethodInfo'); methodStart=(Get-MarkerField $c20Line 'callerMethodStart'); methodEnd=(Get-MarkerField $c20Line 'callerMethodEnd'); gcInfoAttempted=(Get-MarkerField $c20Line 'callerGcInfoAttempted'); gcInfoResult=(Get-MarkerField $c20Line 'callerGcInfoResult'); stackMoved=(Get-MarkerField $c20Line 'callerSpMoved'); stackAligned=(Get-MarkerField $c20Line 'callerSpAligned'); frameDistinct=(Get-MarkerField $c20Line 'callerFrameDistinct') }
                roots=[ordered]@{ currentFrame=(Get-MarkerField $c20Line 'c19RootReports'); register=(Get-MarkerField $c20Line 'c19RegisterRoots'); stack=(Get-MarkerField $c20Line 'c19StackRoots'); promoteAttempts=(Get-MarkerField $c20Line 'c19PromoteAttempts'); promoteEntries=(Get-MarkerField $c20Line 'c19PromoteEntries'); promoteReturns=(Get-MarkerField $c20Line 'c19PromoteReturns') }
                accounting=[ordered]@{ frames=(Get-MarkerField $c20Line 'framesWalked'); callbacks=(Get-MarkerField $c20Line 'stackProviderCallbacks'); totalRoots=(Get-MarkerField $c20Line 'totalRoots'); markWrites=(Get-MarkerField $c20Line 'markWrites'); childReads=(Get-MarkerField $c20Line 'childReads'); graphTraversal=(Get-MarkerField $c20Line 'graphTraversal'); stackBase=(Get-MarkerField $c20Line 'stackBase'); stackLimit=(Get-MarkerField $c20Line 'stackLimit'); scanContextStackLimit=(Get-MarkerField $c20Line 'scanContextStackLimit'); boundsConsumed=(Get-MarkerField $c20Line 'stackBoundsConsumed'); promoteEntries=(Get-MarkerField $c20Line 'promoteEntries'); promoteReturns=(Get-MarkerField $c20Line 'promoteReturns') }
            }
        } elseif ($isC011EC19) {
            Assert-Text $validationText '\[nativeaot-gc-unwind-gc-info\] SAFE_STOP marker=C011EC19' "C011EC19 unwind/GC-info marker"
            $c19Line = (($validationText -split "`n") | Where-Object { $_ -match '\[nativeaot-gc-unwind-gc-info\] SAFE_STOP marker=C011EC19' } | Select-Object -Last 1)
            if ((Get-MarkerField $c19Line 'initialControlPC') -ne '0x0000000010001D3F' -or (Get-MarkerField $c19Line 'methodInfo') -eq '0x0000000000000000' -or (Get-MarkerField $c19Line 'unwindInfo') -eq '0x0000000000000000' -or (Get-MarkerField $c19Line 'gcInfo') -eq '0x0000000000000000') { throw "C011EC19 did not retain genuine managed metadata pointers in $name." }
            if ((Get-MarkerField $c19Line 'findMethodInfoAttempts') -ne '0x00000001' -or (Get-MarkerField $c19Line 'findMethodInfoResults') -ne '0x00000001') { throw "C011EC19 did not retain the C011EC18 FindMethodInfo success in $name." }
            if ((Get-MarkerField $c19Line 'methodStart') -ne '0x0000000010001C20' -or (Get-MarkerField $c19Line 'methodEnd') -ne '0x0000000010001E84') { throw "C011EC19 method interval was not decoded from the runtime function table in $name." }
            if ((Get-MarkerField $c19Line 'unwindEntries') -ne '0x00000001' -or (Get-MarkerField $c19Line 'unwindMetadata') -ne '0x00000001' -or (Get-MarkerField $c19Line 'unwindCompleted') -ne '0x00000001' -or (Get-MarkerField $c19Line 'unwindResult') -ne '0x00000001') { throw "C011EC19 did not complete the genuine unwind boundary in $name." }
            if ((Get-MarkerField $c19Line 'gcInfoLookups') -eq '0x00000000' -or (Get-MarkerField $c19Line 'gcInfoDecodeAttempts') -eq '0x00000000') { throw "C011EC19 did not reach genuine GC-info decoding in $name." }
            $c19DecodeResult = Get-MarkerField $c19Line 'gcInfoDecodeResult'
            $c19Outcome = if ($c19DecodeResult -eq '0x00000001') { 'A' } else { 'B' }
             $runResults += [ordered]@{ name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker='C011EC19'; outcome=$c19Outcome; harnessTerminated=$true; markerLine=$c19Line; frame=[ordered]@{initialControlPC=(Get-MarkerField $c19Line 'initialControlPC');initialSP=(Get-MarkerField $c19Line 'initialSP');initialFP=(Get-MarkerField $c19Line 'initialFP');methodInfo=(Get-MarkerField $c19Line 'methodInfo');findMethodInfoAttempts=(Get-MarkerField $c19Line 'findMethodInfoAttempts');findMethodInfoResults=(Get-MarkerField $c19Line 'findMethodInfoResults');methodStart=(Get-MarkerField $c19Line 'methodStart');methodEnd=(Get-MarkerField $c19Line 'methodEnd')}; unwind=[ordered]@{entries=(Get-MarkerField $c19Line 'unwindEntries');metadata=(Get-MarkerField $c19Line 'unwindMetadata');info=(Get-MarkerField $c19Line 'unwindInfo');infoSize=(Get-MarkerField $c19Line 'unwindInfoSize');blockFlags=(Get-MarkerField $c19Line 'unwindBlockFlags');rtlVirtualUnwind=(Get-MarkerField $c19Line 'unwindRtl');completed=(Get-MarkerField $c19Line 'unwindCompleted');result=(Get-MarkerField $c19Line 'unwindResult');callerControlPC=(Get-MarkerField $c19Line 'callerControlPC');callerSP=(Get-MarkerField $c19Line 'callerSP');callerFP=(Get-MarkerField $c19Line 'callerFP');previousTransitionFrame=(Get-MarkerField $c19Line 'previousTransitionFrame');preservedRegisters=(Get-MarkerField $c19Line 'preservedRegisters')}; gcInfo=[ordered]@{lookups=(Get-MarkerField $c19Line 'gcInfoLookups');pointer=(Get-MarkerField $c19Line 'gcInfo');safePoint=(Get-MarkerField $c19Line 'safePoint');codeOffset=(Get-MarkerField $c19Line 'codeOffset');decodeAttempts=(Get-MarkerField $c19Line 'gcInfoDecodeAttempts');decodeResult=$c19DecodeResult;interruptible=(Get-MarkerField $c19Line 'interruptible');interruptibleRanges=(Get-MarkerField $c19Line 'interruptibleRanges')}; roots=[ordered]@{reports=(Get-MarkerField $c19Line 'rootReports');registerRoots=(Get-MarkerField $c19Line 'registerRoots');stackRoots=(Get-MarkerField $c19Line 'stackRoots');firstKind=(Get-MarkerField $c19Line 'firstRootKind');firstSlot=(Get-MarkerField $c19Line 'firstRootSlot');firstValue=(Get-MarkerField $c19Line 'firstRootValue');firstStackSlot=(Get-MarkerField $c19Line 'firstStackRootSlot');firstStackValue=(Get-MarkerField $c19Line 'firstStackRootValue')}; promotion=[ordered]@{firstStackAttempts=(Get-MarkerField $c19Line 'firstStackPromoteAttempts');firstStackEntries=(Get-MarkerField $c19Line 'firstStackPromoteEntries');firstStackReturns=(Get-MarkerField $c19Line 'firstStackPromoteReturns');entries=(Get-MarkerField $c19Line 'promoteEntries');returns=(Get-MarkerField $c19Line 'promoteReturns')}; queue=[ordered]@{firstSlot=(Get-MarkerField $c19Line 'firstQueueSlot');firstCursorBefore=(Get-MarkerField $c19Line 'firstQueueCursorBefore');firstCursorAfter=(Get-MarkerField $c19Line 'firstQueueCursorAfter');firstNew=(Get-MarkerField $c19Line 'firstQueueNew');secondInsertions=(Get-MarkerField $c19Line 'secondQueueInsertions');secondSlot=(Get-MarkerField $c19Line 'secondQueueSlot');secondCursorBefore=(Get-MarkerField $c19Line 'secondQueueCursorBefore');secondCursorAfter=(Get-MarkerField $c19Line 'secondQueueCursorAfter');secondOld=(Get-MarkerField $c19Line 'secondQueueOld');secondNew=(Get-MarkerField $c19Line 'secondQueueNew')}; graph=[ordered]@{markWrites=(Get-MarkerField $c19Line 'markWrites');childReads=(Get-MarkerField $c19Line 'childReads');traversal=(Get-MarkerField $c19Line 'graphTraversal')}; bounds=[ordered]@{stackBase=(Get-MarkerField $c19Line 'stackBase');stackLimit=(Get-MarkerField $c19Line 'stackLimit');scanContextStackLimit=(Get-MarkerField $c19Line 'scanContextStackLimit');consumed=(Get-MarkerField $c19Line 'stackBoundsConsumed')}; existingObjectGraph=[ordered]@{sentinel=(Get-MarkerField $c19Line 'sentinel');storageObject=(Get-MarkerField $c19Line 'storageObject');queueSlot=$null;queueCursor=$null;markWrites=(Get-MarkerField $c19Line 'markWrites');childReads=(Get-MarkerField $c19Line 'childReads');traversal=(Get-MarkerField $c19Line 'graphTraversal')}; runtime=[ordered]@{currentThread=(Get-MarkerField $c19Line 'currentThread');enumeratedThread=(Get-MarkerField $c19Line 'enumeratedThread');initiatorThread=(Get-MarkerField $c19Line 'initiatorThread');threadStoreOwner=(Get-MarkerField $c19Line 'threadStoreOwner');threadStoreRecursion=(Get-MarkerField $c19Line 'threadStoreRecursion');eeSuspended=(Get-MarkerField $c19Line 'eeSuspended');managedEntryProhibited=(Get-MarkerField $c19Line 'managedEntryProhibited');cooperative=(Get-MarkerField $c19Line 'cooperative');preemptive=(Get-MarkerField $c19Line 'preemptive');threadUnderCrawl=(Get-MarkerField $c19Line 'threadUnderCrawl');restart=(Get-MarkerField $c19Line 'restart');resume=(Get-MarkerField $c19Line 'resume')} }
              $runResults[-1].existingObjectGraph.queueSlot = Get-MarkerField $c19Line 'firstQueueSlot'
              $runResults[-1].existingObjectGraph.queueCursor = Get-MarkerField $c19Line 'firstQueueCursorAfter'
              $runResults[-1].existingObjectGraph.queueValue = Get-MarkerField $c19Line 'firstQueueNew'
              $runResults[-1].promotion.legacySecondAttempts = Get-MarkerField $c19Line 'legacySecondPromoteAttempts'
              $runResults[-1].promotion.legacySecondEntries = Get-MarkerField $c19Line 'legacySecondPromoteEntries'
         } elseif ($isTransitionFrameControlPc) {
            $preflightLine = (($validationText -split "`n") | Where-Object { $_ -match '\[nativeaot-code-manager\] preflight .*marker=C011EC17-PREFLIGHT' } | Select-Object -Last 1)
            $transitionPreflightLine = (($validationText -split "`n") | Where-Object { $_ -match '\[nativeaot-code-manager\] C011EC18-PREFLIGHT .*marker=C011EC18-PREFLIGHT' } | Select-Object -Last 1)
            $iteratorInitialLine = (($validationText -split "`n") | Where-Object { $_ -match '\[nativeaot-code-manager\] C011EC18 iterator-initial .*marker=C011EC18-ITERATOR' } | Select-Object -Last 1)
            $directMarkerLine = (($validationText -split "`n") | Where-Object { $_ -match '\[nativeaot-code-manager\] transition-frame-control-pc .*marker=C011EC18' } | Select-Object -Last 1)
            $findMethodLine = (($validationText -split "`n") | Where-Object { $_ -match '\[nativeaot-code-manager\] C011EC18 FindMethodInfo .*marker=C011EC18-FIND-METHOD' } | Select-Object -Last 1)
            $c15Line = (($validationText -split "`n") | Where-Object { $_ -match '\[nativeaot-gc-next-genuine-root-provider\] SAFE_STOP marker=C011EC15' } | Select-Object -Last 1)
            foreach ($evidence in @(@($preflightLine, 'C011EC17 preflight'), @($transitionPreflightLine, 'C011EC18 transition preflight'), @($iteratorInitialLine, 'C011EC18 iterator initial'), @($directMarkerLine, 'C011EC18 direct marker'), @($findMethodLine, 'C011EC18 FindMethodInfo'), @($c15Line, 'C011EC15 post-transition boundary'))) {
                if ([string]::IsNullOrWhiteSpace($evidence[0])) { throw "Missing $($evidence[1]) evidence in $name." }
            }
            $preflightFields = ($preflightLine -split '\[nativeaot-code-manager\] preflight ', 2)[-1]
            $transitionFields = ($transitionPreflightLine -split '\[nativeaot-code-manager\] C011EC18-PREFLIGHT ', 2)[-1]
            $directFields = ($directMarkerLine -split '\[nativeaot-code-manager\] transition-frame-control-pc ', 2)[-1]
            $registeredLine = (($validationText -split "`n") | Where-Object { $_ -match '\[nativeaot-code-manager\] registered module=' } | Select-Object -Last 1)
            if ([string]::IsNullOrWhiteSpace($registeredLine)) { throw "C011EC17 registration evidence was missing in C011EC18 run $name." }
            $registeredManager = Get-MarkerField $registeredLine 'manager'
            $transitionInRange = Get-MarkerField $transitionFields 'transitionInRange'
            $transitionFrame = Get-MarkerField $transitionFields 'frame'
            $transitionRip = Get-MarkerField $transitionFields 'transitionRIP'
            $savedRsp = Get-MarkerField $transitionFields 'savedRSP'
            $iteratorControlPc = Get-MarkerField $directFields 'iteratorControlPC'
            $iteratorManager = Get-MarkerField $directFields 'manager'
            $authenticManager = Get-MarkerField $directFields 'authenticManager'
            $methodInfo = Get-MarkerField $directFields 'methodInfo'
            $methodInfoResult = Get-MarkerField $directFields 'methodInfoResult'
            if ($transitionInRange -ne '0x00000001' -or
                $transitionFrame -eq '0x0000000000000000' -or
                $transitionRip -eq '0x0000000000000000' -or
                $transitionRip -ne $iteratorControlPc -or
                $iteratorManager -eq '0x0000000000000000' -or
                $iteratorManager -ne $authenticManager -or
                $iteratorManager -ne $registeredManager -or
                $methodInfo -eq '0x0000000000000000' -or
                $methodInfoResult -ne '0x00000001') {
                throw "C011EC18 did not prove the structurally saved managed PC and production code-manager lookup in $name."
            }
            Assert-Text $findMethodLine 'result=00000001' "successful NativeAOT FindMethodInfo"
            Assert-Text $c15Line 'c18FindMethodInfoSuccess=00000001' "C011EC18 method metadata success at C011EC15 boundary"
            Assert-Text $c15Line 'c18TransitionInRange=00000001' "C011EC18 managed-range membership"
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker='C011EC15'; outcome='A'; harnessTerminated=$true
                registration=[ordered]@{ marker=$registeredLine; preflight=$preflightLine; manager=$registeredManager; managedStart=(Get-MarkerField $preflightFields 'managedStart'); managedSize=(Get-MarkerField $preflightFields 'managedSize'); managedEnd=(Get-MarkerField $preflightFields 'managedEnd'); originalOutOfRangePC='0x00000000100547F6'; originalOutOfRangeMembership='false' }
                transition=[ordered]@{ frame=$transitionFrame; currentNativeRIP=(Get-MarkerField $transitionFields 'currentRIP'); currentNativeReturnSlot=(Get-MarkerField $transitionFields 'currentReturnSlot'); transitionRIP=$transitionRip; transitionRBP=(Get-MarkerField $transitionFields 'transitionRBP'); frameThread=(Get-MarkerField $transitionFields 'frameThread'); thread=(Get-MarkerField $transitionFields 'thread'); flags=(Get-MarkerField $transitionFields 'flags'); savedRSP=$savedRsp; savedRBP=(Get-MarkerField $transitionFields 'savedRBP'); transitionInManagedRange=$transitionInRange; nativeManager=(Get-MarkerField $transitionFields 'nativeManager'); transitionManager=(Get-MarkerField $transitionFields 'transitionManager') }
                iterator=[ordered]@{ initialControlPC=$iteratorControlPc; initialSP=(Get-MarkerField $directFields 'iteratorSP'); initialFP=(Get-MarkerField $directFields 'iteratorFP'); codeManager=$iteratorManager; methodInfo=$methodInfo; framePointer=(Get-MarkerField $c15Line 'c18FramePointer'); unwindSteps=(Get-MarkerField $c15Line 'c18UnwindSteps'); framesWalked=(Get-MarkerField $c15Line 'c18StackFrames') }
                methodInfo=[ordered]@{ attempted=$true; result='success'; metadataValid=(Get-MarkerField $c15Line 'c18MetadataValid'); findMethodInfoAttempts=(Get-MarkerField $c15Line 'c18FindMethodInfoAttempts'); findMethodInfoSuccess=(Get-MarkerField $c15Line 'c18FindMethodInfoSuccess') }
                stack=[ordered]@{ providerCallbacks=(Get-MarkerField $c15Line 'c18StackProviderCallbacks'); rootSlots=(Get-MarkerField $c15Line 'c18StackRootSlots'); secondPromoteAttempts=(Get-MarkerField $c15Line 'secondPromoteAttempts'); secondPromoteEntries=(Get-MarkerField $c15Line 'secondPromoteEntries'); secondQueueInsertions=(Get-MarkerField $c15Line 'secondQueueMutationExecutions'); stackBoundsConsumed=(Get-MarkerField $c15Line 'c18StackBoundsConsumed') }
                queue=[ordered]@{ firstSlot=(Get-MarkerField $c15Line 'firstQueueSlot'); slotIndex=(Get-MarkerField $c15Line 'firstQueueSlotIndex'); cursorBefore=(Get-MarkerField $c15Line 'firstQueueCursorBefore'); cursorAfter=(Get-MarkerField $c15Line 'firstQueueCursorAfter'); old=(Get-MarkerField $c15Line 'firstQueueOld'); new=(Get-MarkerField $c15Line 'firstQueueNew'); cursor=(Get-MarkerField $c15Line 'firstQueueCursorAfter') }
                root=[ordered]@{ sentinel=(Get-MarkerField $c15Line 'sentinel'); storageObject=(Get-MarkerField $c15Line 'storageObject'); firstRoot=(Get-MarkerField $c15Line 'firstRootRaw'); nextRoot=(Get-MarkerField $c15Line 'nextRootRaw') }
                invariants=[ordered]@{ lockHeld=(Get-MarkerField $c15Line 'threadStoreLockHeld'); eeSuspended=(Get-MarkerField $c15Line 'eeSuspended'); managedEntryProhibited=(Get-MarkerField $c15Line 'managedEntryProhibited'); restart=(Get-MarkerField $c15Line 'restart'); resume=(Get-MarkerField $c15Line 'resume'); markBitWrites=(Get-MarkerField $c15Line 'markBitWrites'); childReferenceReads=(Get-MarkerField $c15Line 'childReferenceReads'); graphTraversal=(Get-MarkerField $c15Line 'graphTraversal') }
                c011ec18=[ordered]@{ directMarker=$directMarkerLine; iteratorInitial=$iteratorInitialLine; findMethodInfo=$findMethodLine }
            }
        } elseif ($isCodeManagerRegistration) {
            $preflightLine = (($validationText -split "`n") | Where-Object { $_ -match '\[nativeaot-code-manager\] preflight .*marker=C011EC17-PREFLIGHT' } | Select-Object -Last 1)
            if ([string]::IsNullOrWhiteSpace($preflightLine)) { throw "C011EC17 did not emit its allocation-free preflight marker in $name." }
            $preflightFields = ($preflightLine -split '\[nativeaot-code-manager\] preflight ', 2)[-1]
            Assert-Text $preflightLine 'registration=00000001' "one production code-manager registration"
            $managedSize = Get-MarkerField $preflightFields 'managedSize'
            if ($managedSize -eq $null -or $managedSize -eq '0x0000000000000000') { throw "C011EC17 did not report a nonzero managed-code range in $name." }
            $c17RegistrationLine = (($validationText -split "`n") | Where-Object { $_ -match '\[nativeaot-code-manager\] registered module=' } | Select-Object -Last 1)
            if ([string]::IsNullOrWhiteSpace($c17RegistrationLine)) { throw "C011EC17 did not emit its startup registration marker in $name." }
            $registeredManager = Get-MarkerField $c17RegistrationLine 'manager'
            if ($registeredManager -eq $null -or $registeredManager -eq '0x0000000000000000') { throw "C011EC17 did not retain a production code-manager pointer in $name." }
            $preflightManager = Get-MarkerField $preflightFields 'manager'
            $lookup = Get-MarkerField $preflightFields 'lookup'
            if ($preflightManager -ne $lookup) { throw "C011EC17 preflight manager and lookup fields disagree in $name." }
            $isManaged = Get-MarkerField $preflightFields 'isManaged'
            if ($isManaged -eq '0x0000000000000001' -and ($lookup -eq '0x0000000000000000' -or $lookup -ne $registeredManager)) { throw "C011EC17 reported an inconsistent in-range code-manager lookup in $name." }
            $c15Line = (($validationText -split "`n") | Where-Object { $_ -match '\[nativeaot-gc-next-genuine-root-provider\] SAFE_STOP marker=C011EC15' } | Select-Object -Last 1)
            $failFastReached = $validationText -match '\[nativeaot-pal-qemu-test\] FAIL_FAST reason=47435354'
            if ([string]::IsNullOrWhiteSpace($c15Line) -and -not $failFastReached) { throw "C011EC17 stopped without either the post-lookup boundary or a genuine C011EC15 advance in $name." }
            $lookupSucceeded = $isManaged -eq '0x0000000000000001' -and $lookup -eq $registeredManager
            $runOutcome = if ($c15Line) { "A" } elseif ($lookupSucceeded) { "B" } else { "D" }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker=if ($c15Line) { "C011EC15" } else { $null }; outcome=$runOutcome; harnessTerminated=$true
                registration=[ordered]@{ marker=$c17RegistrationLine; runtime=(Get-MarkerField $preflightFields 'runtime'); manager=$registeredManager; managedStart=(Get-MarkerField $preflightFields 'managedStart'); managedSize=$managedSize; managedEnd=(Get-MarkerField $preflightFields 'managedEnd'); controlPC=(Get-MarkerField $preflightFields 'controlPC'); isManaged=$isManaged; lookup=$lookup; registrationCount=(Get-MarkerField $preflightFields 'registration') }
                methodInfo=[ordered]@{ attempted=$lookupSucceeded; result=if ($c15Line) { "succeeded (stack iterator crossed the prior null lookup boundary)" } elseif ($lookupSucceeded) { "failed or immediately related metadata boundary after genuine lookup" } else { "not attempted; IsManaged(controlPC) was false and GetCodeManagerForAddress returned null" }; metadataValid=[bool]$c15Line; framePointerCalculationReached=[bool]$c15Line }
                stack=[ordered]@{ framesWalked=if ($c15Line) { "at least 1" } else { 0 }; providerCallbacks=if ($c15Line) { (Get-MarkerField $c15Line 'providerEntries') } else { "0x00000000" }; rootSlots=if ($c15Line) { (Get-MarkerField $c15Line 'rootSlotsVisited') } else { "0x00000000" }; secondPromoteAttempts=if ($c15Line) { (Get-MarkerField $c15Line 'secondPromoteAttempts') } else { "0x00000000" }; secondPromoteEntries=if ($c15Line) { (Get-MarkerField $c15Line 'secondPromoteEntries') } else { "0x00000000" }; secondQueueInsertions=if ($c15Line) { (Get-MarkerField $c15Line 'secondQueueMutationExecutions') } else { "0x00000000" } }
                c011ec15=if ($c15Line) { $c15Line } else { $null }; failFast=if ($failFastReached) { "0x47435354" } else { $null }; debugLog=$qemuDebugPath
            }
        } elseif ($isStackProviderTransitionFailFast) {
            Assert-Text $validationText '\[nativeaot-gc-stack-provider-transition-failfast\] ordinary-provider-returned-null' "minimal ordinary ThreadStatic provider return"
            Assert-Text $validationText '\[nativeaot-gc-stack-provider-transition-failfast\] stack-provider-transition-start' "minimal stack-provider transition start"
            Assert-Text $validationText '\[nativeaot-pal-qemu-test\] FAIL_FAST reason=47435354' "NativeAOT transition fail-fast"
            if ($validationText -match 'SAFE_STOP marker=C011EC15|stack root|GcInfo|EnumGcRefsCallback') { throw "Minimal transition run reached stack-root semantic evidence in $name." }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker=$null; outcome="D"; harnessTerminated=$true; earlyFailure=$earlyFailure
                failure=[ordered]@{ classification="Thread::GcScanRoots-prologue-code-manager-invariant"; palReason="0x47435354"; entry="Thread::GcScanRoots"; stackCallbackCount=0; stackFramesWalked=0; gcInfoReads=0 }
                transition=[ordered]@{ ordinaryProviderReturn="null"; category3TransitionStart=$true; failFast=$true }
            }
        } elseif ($isNextGenuineRootProvider) {
            Assert-Text $validationText '\[nativeaot-gc-next-genuine-root-provider\] SAFE_STOP marker=C011EC15' "C011EC15 next genuine root marker"
            $c15Line = (($validationText -split "`n") | Where-Object { $_ -match '\[nativeaot-gc-next-genuine-root-provider\] SAFE_STOP marker=C011EC15' } | Select-Object -Last 1)
            if ([string]::IsNullOrWhiteSpace($c15Line)) { throw "C011EC15 marker line was not isolated in $name." }
            Assert-Text $c15Line 'nonNullCandidates=00000002' "two genuine non-null root candidates"
            Assert-Text $c15Line 'firstRootCallbackReturns=00000001' "first root callback returned once"
            Assert-Text $c15Line 'queueMarkReturns=00000001' "first queue_mark returned once"
            Assert-Text $c15Line 'secondPromoteAttempts=00000000 secondPromoteEntries=00000000 secondQueueMutationAttempts=00000000 secondQueueMutationExecutions=00000000' "second root stopped before Promote and queue mutation"
            Assert-Text $c15Line 'markBitWrites=00000000 childReferenceReads=00000000 graphTraversal=00000000' "no mark-bit or child traversal mutation"
            Assert-Text $c15Line 'threadStoreLockHeld=00000001 eeSuspended=00000001 managedEntryProhibited=00000001 restart=00000000 resume=00000000' "suspended EE invariants"
            $firstRootRaw = Get-MarkerField $c15Line 'firstRootRaw'
            $storageObject = Get-MarkerField $c15Line 'storageObject'
            $nextRootRaw = Get-MarkerField $c15Line 'nextRootRaw'
            if ($firstRootRaw -eq '0x0000000000000000' -or $firstRootRaw -ne $storageObject -or $nextRootRaw -eq '0x0000000000000000') { throw "C011EC15 did not identify the genuine first storage root and a non-null next candidate in $name." }
            if ((Get-MarkerField $c15Line 'firstRootProviderCategory') -ne '0x00000001') { throw "C011EC15 first root did not come from the inline thread-static provider in $name." }
            if ((Get-MarkerField $c15Line 'nextRootProviderCategory') -eq '0x00000000') { throw "C011EC15 next provider category was empty in $name." }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker="C011EC15"; outcome="A"; harnessTerminated=$true
                root=[ordered]@{slot=(Get-MarkerField $c15Line 'firstRootSlot');rawValue=$firstRootRaw;storageObject=$storageObject;sentinel=(Get-MarkerField $c15Line 'sentinel');providerCategory=(Get-MarkerField $c15Line 'firstRootProviderCategory');provider=(Get-MarkerField $c15Line 'firstRootProvider');callback=(Get-MarkerField $c15Line 'firstRootCallback');context=(Get-MarkerField $c15Line 'firstRootContext')}
                nextRoot=[ordered]@{slot=(Get-MarkerField $c15Line 'nextRootSlot');rawValue=$nextRootRaw;providerCategory=(Get-MarkerField $c15Line 'nextRootProviderCategory');provider=(Get-MarkerField $c15Line 'nextRootProvider');callback=(Get-MarkerField $c15Line 'nextRootCallback');context=(Get-MarkerField $c15Line 'nextRootContext')}
                accounting=[ordered]@{providerRequests=(Get-MarkerField $c15Line 'providerRequests');providerEntries=(Get-MarkerField $c15Line 'providerEntries');gcScanRootsRequests=(Get-MarkerField $c15Line 'gcScanRootsRequests');rootSlotsVisited=(Get-MarkerField $c15Line 'rootSlotsVisited');nullCandidates=(Get-MarkerField $c15Line 'nullCandidates');nonNullCandidates=(Get-MarkerField $c15Line 'nonNullCandidates');firstRootCallbackReturns=(Get-MarkerField $c15Line 'firstRootCallbackReturns');enumGcRefContinuations=(Get-MarkerField $c15Line 'enumGcRefContinuations');promoteReturns=(Get-MarkerField $c15Line 'promoteReturns');markHelperReturns=(Get-MarkerField $c15Line 'markHelperReturns');queueMarkReturns=(Get-MarkerField $c15Line 'queueMarkReturns')}
                queue=[ordered]@{slot=(Get-MarkerField $c15Line 'firstQueueSlot');slotIndex=(Get-MarkerField $c15Line 'firstQueueSlotIndex');old=(Get-MarkerField $c15Line 'firstQueueOld');new=(Get-MarkerField $c15Line 'firstQueueNew');cursorBefore=(Get-MarkerField $c15Line 'firstQueueCursorBefore');cursorAfter=(Get-MarkerField $c15Line 'firstQueueCursorAfter');base=(Get-MarkerField $c15Line 'firstQueueBase')}
                prohibited=[ordered]@{secondPromoteAttempts=(Get-MarkerField $c15Line 'secondPromoteAttempts');secondPromoteEntries=(Get-MarkerField $c15Line 'secondPromoteEntries');secondQueueMutationAttempts=(Get-MarkerField $c15Line 'secondQueueMutationAttempts');secondQueueMutationExecutions=(Get-MarkerField $c15Line 'secondQueueMutationExecutions');markBitWrites=(Get-MarkerField $c15Line 'markBitWrites');childReferenceReads=(Get-MarkerField $c15Line 'childReferenceReads');graphTraversal=(Get-MarkerField $c15Line 'graphTraversal')}
                threadStore=[ordered]@{lockHeld=(Get-MarkerField $c15Line 'threadStoreLockHeld');eeSuspended=(Get-MarkerField $c15Line 'eeSuspended');managedEntryProhibited=(Get-MarkerField $c15Line 'managedEntryProhibited');restart=(Get-MarkerField $c15Line 'restart');resume=(Get-MarkerField $c15Line 'resume')}
                providerOrder=[ordered]@{first=(Get-MarkerField $c15Line 'firstRootProviderCategory');next=(Get-MarkerField $c15Line 'nextRootProviderCategory')}
            }
        } elseif ($isFirstRootFirstNonNullOldO) {
            if ($earlyFailure -eq "c011ec14-no-non-null-old-o-before-nativeaot-failfast") {
                Assert-Text $validationText '\[nativeaot-pal-qemu-test\] FAIL_FAST reason=47435354' "NativeAOT bounded continuation fail-fast"
                $preDecisionLine = (($validationText -split "`n") | Where-Object { $_ -match '\[nativeaot-gc-first-allocation\] c14-predecision' } | Select-Object -Last 1)
                if ([string]::IsNullOrWhiteSpace($preDecisionLine)) { throw "C011EC14 fail-fast evidence did not include the pre-decision counter line in $name." }
                Assert-Text $preDecisionLine 'queueInsertions=00000001 callbacks=00000001 .*old_o=0000000000000000 rawMarkReads=00000000' "single valid queue insertion before bounded continuation failure"
                $runResults += [ordered]@{
                    name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker=$null; outcome="E"; harnessTerminated=$true; earlyFailure=$earlyFailure
                    failure=[ordered]@{ classification="nativeaot-continuation-failfast-before-second-queue-insertion"; palReason="0x47435354"; preDecision=$preDecisionLine }
                    root=[ordered]@{slot=(Get-MarkerField $preDecisionLine 'rootSlot');rawValue=(Get-MarkerField $preDecisionLine 'rawRoot');storageObject=(Get-MarkerField $preDecisionLine 'storageObject')}
                    queue=[ordered]@{insertions=(Get-MarkerField $preDecisionLine 'queueInsertions');capacity="0x0000000000000010";oldObject="0x0000000000000000";markedReads=(Get-MarkerField $preDecisionLine 'rawMarkReads')}
                    decision=[ordered]@{oldObject="0x0000000000000000";nonNullReached=$false}
                    markState=[ordered]@{reads=(Get-MarkerField $preDecisionLine 'rawMarkReads');writes="0x00000000";result="not reached"}
                    prohibited=[ordered]@{graphTraversal="not reached";childReferenceReads="not reached";childObjects="not reached";callbackReturnsAfterTarget="not applicable";restart="not reached";resume="not reached"}
                }
                continue
            }
            Assert-Text $validationText '\[nativeaot-gc-first-root-first-non-null-old-o\] SAFE_STOP marker=C011EC14' "C011EC14 first non-null old_o marker"
            $nonNullOldOLine = (($validationText -split "`n") | Where-Object { $_ -match '\[nativeaot-gc-first-root-first-non-null-old-o\] SAFE_STOP marker=C011EC14' } | Select-Object -Last 1)
            Assert-Text $nonNullOldOLine 'queueInsertions=00000011 queueWrites=00000011 queueHistoryOverflow=00000000' "17 real queue insertions"
            Assert-Text $nonNullOldOLine 'nullDecisions=00000010 nonNullDecisions=00000001 decisionRequests=00000011 decisionEntries=00000011' "16 null decisions followed by first non-null old_o"
            Assert-Text $nonNullOldOLine 'markedRequests=00000001 markedEntries=00000001 markedReturns=00000001 markStateReads=00000001 rawMarkWordReads=00000001' "actual marked(old_o) read"
            Assert-Text $nonNullOldOLine 'newMutationAttempts=00000000 newMutationExecutions=00000000 markBitWrites=00000000 graphTraversal=00000000 childReferenceReads=00000000 childObjectsDiscovered=00000000 secondObjectMarkAttempts=00000000' "no later mutation or traversal"
            Assert-Text $nonNullOldOLine 'restart=00000000 resume=00000000 lockHeld=00000001 eeSuspended=00000001 managedEntryProhibited=00000001' "no restart/resume and suspended invariants"
            $oldObject = Get-MarkerField $nonNullOldOLine 'old_o'
            if ($oldObject -eq '0x0000000000000000' -or $oldObject -ne (Get-MarkerField $nonNullOldOLine 'slotOld') -or $oldObject -eq (Get-MarkerField $nonNullOldOLine 'sentinel')) { throw "C011EC14 did not select a genuine non-null displaced old_o in $name." }
            if ((Get-MarkerField $nonNullOldOLine 'slotIndex') -ne '0x0000000000000000' -or (Get-MarkerField $nonNullOldOLine 'cursorBefore') -ne '0x0000000000000000' -or (Get-MarkerField $nonNullOldOLine 'cursorAfter') -ne '0x0000000000000001') { throw "C011EC14 did not capture the slot-zero ring wrap in $name." }
            if ((Get-MarkerField $nonNullOldOLine 'provenanceValid') -ne '0x00000001' -or (Get-MarkerField $nonNullOldOLine 'findRange') -ne '0x00000001' -or (Get-MarkerField $nonNullOldOLine 'heapMembership') -ne '0x00000001') { throw "C011EC14 old_o provenance was not valid in $name." }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker="C011EC14"; outcome="A"; harnessTerminated=$true
                root=[ordered]@{slot=(Get-MarkerField $nonNullOldOLine 'rootSlot');rawValue=(Get-MarkerField $nonNullOldOLine 'rawRoot');storageObject=(Get-MarkerField $nonNullOldOLine 'storageObject');sentinel=(Get-MarkerField $nonNullOldOLine 'sentinel')}
                queue=[ordered]@{insertions=(Get-MarkerField $nonNullOldOLine 'queueInsertions');writes=(Get-MarkerField $nonNullOldOLine 'queueWrites');slotAddress=(Get-MarkerField $nonNullOldOLine 'selectedSlot');slotIndex=(Get-MarkerField $nonNullOldOLine 'slotIndex');oldSlot=(Get-MarkerField $nonNullOldOLine 'slotOld');newSlot=(Get-MarkerField $nonNullOldOLine 'slotNew');oldObject=$oldObject;cursorBefore=(Get-MarkerField $nonNullOldOLine 'cursorBefore');cursorAfter=(Get-MarkerField $nonNullOldOLine 'cursorAfter');base=(Get-MarkerField $nonNullOldOLine 'queueBase')}
                decision=[ordered]@{requests=(Get-MarkerField $nonNullOldOLine 'decisionRequests');entries=(Get-MarkerField $nonNullOldOLine 'decisionEntries');nullDecisions=(Get-MarkerField $nonNullOldOLine 'nullDecisions');nonNullDecisions=(Get-MarkerField $nonNullOldOLine 'nonNullDecisions');oldObject=$oldObject}
                markState=[ordered]@{requests=(Get-MarkerField $nonNullOldOLine 'markedRequests');entries=(Get-MarkerField $nonNullOldOLine 'markedEntries');returns=(Get-MarkerField $nonNullOldOLine 'markedReturns');reads=(Get-MarkerField $nonNullOldOLine 'markStateReads');result=(Get-MarkerField $nonNullOldOLine 'markedResult');rawReads=(Get-MarkerField $nonNullOldOLine 'rawMarkWordReads');rawHeader=(Get-MarkerField $nonNullOldOLine 'rawHeader');mask=(Get-MarkerField $nonNullOldOLine 'markMask')}
                provenance=[ordered]@{valid=(Get-MarkerField $nonNullOldOLine 'provenanceValid');findRange=(Get-MarkerField $nonNullOldOLine 'findRange');heapMembership=(Get-MarkerField $nonNullOldOLine 'heapMembership');generation=(Get-MarkerField $nonNullOldOLine 'generation');objectHistoryIndex=(Get-MarkerField $nonNullOldOLine 'objectHistoryIndex')}
                counters=[ordered]@{callbacks=(Get-MarkerField $nonNullOldOLine 'callbacks');candidateLoads=(Get-MarkerField $nonNullOldOLine 'candidateLoads');markHelpers=(Get-MarkerField $nonNullOldOLine 'markHelpers');callbackReturnsBeforeDecision=(Get-MarkerField $nonNullOldOLine 'callbackReturnsBeforeDecision');callbackReturns=(Get-MarkerField $nonNullOldOLine 'callbackReturns')}
                mutation=[ordered]@{newAttempts=(Get-MarkerField $nonNullOldOLine 'newMutationAttempts');newExecutions=(Get-MarkerField $nonNullOldOLine 'newMutationExecutions');markBitWrites=(Get-MarkerField $nonNullOldOLine 'markBitWrites')}
                traversal=[ordered]@{graph=(Get-MarkerField $nonNullOldOLine 'graphTraversal');childReferenceReads=(Get-MarkerField $nonNullOldOLine 'childReferenceReads');childObjects=(Get-MarkerField $nonNullOldOLine 'childObjectsDiscovered');secondObjectMarkAttempts=(Get-MarkerField $nonNullOldOLine 'secondObjectMarkAttempts')}
            }
        } elseif ($isFirstRootPostQueueMarkDecision) {
            Assert-Text $validationText '\[nativeaot-gc-first-root-post-queue-mark-decision\] SAFE_STOP marker=C011EC13' "first root post-queue decision marker"
            $postQueueLine = (($validationText -split "`n") | Where-Object { $_ -match '\[nativeaot-gc-first-root-post-queue-mark-decision\] SAFE_STOP marker=C011EC13' } | Select-Object -Last 1)
            if ([string]::IsNullOrWhiteSpace($postQueueLine)) { throw "C011EC13 marker line was not isolated in $name." }
            Assert-Text $postQueueLine 'membership=00000001 wksMultipleHeaps=00000000 hpt=0000000000000000 heapOf=0000000000000000 heapNumber=00000000 heapCount=00000001 wksNullHeapValid=00000001 condemned=00000001' "membership, WKS null heap, and condemned=true"
            Assert-Text $postQueueLine 'decisionRequests=00000001 decisionEntries=00000001 decisionCompletions=00000001 decisionDuplicates=00000000' "exactly one post-queue decision"
            Assert-Text $postQueueLine 'inheritedQueueSlotWrites=00000001 inheritedCursorWrites=00000001 newMutationAttempts=00000000 newMutationExecutions=00000000' "C011EC12 inherited queue unit and zero new mutation"
            Assert-Text $postQueueLine 'markBitWrites=00000000 logicalMarkComplete=00000000 traversalScheduled=00000000 graphTraversal=00000000 childReferenceReads=00000000 childObjectsDiscovered=00000000 secondObjectMarkAttempts=00000000' "no logical mark or traversal"
            Assert-Text $postQueueLine 'promotionStart=00000000 promotions=00000000 callbackReturns=00000000 secondCallbacks=00000000 restart=00000000 resume=00000000' "no continuation beyond the decision"
            Assert-Text $postQueueLine 'lockHeld=00000001 eeSuspended=00000001 managedEntryProhibited=00000001 registeredThreads=00000001 enumeratedThreads=00000001 includedThreads=00000001 registryMutation=00000000 allocationContextsCleared=00000001' "suspended one-mutator invariants"
            Assert-Text $postQueueLine 'sentinelFailures=00000000 objectValidationBeforeFixup=00000000 objectValidationAfterFixup=00000000 duplicateObjectAddresses=00000000 objectHistoryOverflow=00000000' "object and sentinel validation"
            $postQueueRoot = Get-MarkerField $postQueueLine 'rawRoot'
            $postQueueStorage = Get-MarkerField $postQueueLine 'storageObject'
            $postQueueSentinel = Get-MarkerField $postQueueLine 'sentinel'
            if ($postQueueRoot -ne $postQueueStorage -or $postQueueRoot -eq $postQueueSentinel) { throw "C011EC13 root identity did not remain the genuine storage object in $name." }
            $postQueueOld = Get-MarkerField $postQueueLine 'old_o'
            $postQueueSlotOld = Get-MarkerField $postQueueLine 'slotOld'
            $postQueueNew = Get-MarkerField $postQueueLine 'slotNew'
            if ($postQueueOld -ne $postQueueSlotOld -or $postQueueNew -ne $postQueueStorage) { throw "C011EC13 queue old/new values were inconsistent in $name." }
            if ((Get-MarkerField $postQueueLine 'slotIndex') -ne '0x0000000000000000' -or
                (Get-MarkerField $postQueueLine 'cursorBefore') -ne '0x0000000000000000' -or
                (Get-MarkerField $postQueueLine 'cursorAfter') -ne '0x0000000000000001') { throw "C011EC13 queue cursor/slot state diverged in $name." }
            $nullResult = Get-MarkerField $postQueueLine 'nullResult'
            $markedRequests = Get-MarkerField $postQueueLine 'markedRequests'
            $markedEntries = Get-MarkerField $postQueueLine 'markedEntries'
            $markedReturns = Get-MarkerField $postQueueLine 'markedReturns'
            $markStateReads = Get-MarkerField $postQueueLine 'markStateReads'
            $markedResult = Get-MarkerField $postQueueLine 'markedResult'
            if ($nullResult -eq '0x00000001') {
                if ($markedRequests -ne '0x00000000' -or $markedEntries -ne '0x00000000' -or $markedReturns -ne '0x00000000' -or $markStateReads -ne '0x00000000' -or (Get-MarkerField $postQueueLine 'branch') -ne '0x00000001') { throw "C011EC13 null path did not bypass marked(old_o) in $name." }
                $postQueueOutcome = 'B'
            } else {
                if ($markedRequests -ne '0x00000001' -or $markedEntries -ne '0x00000001' -or $markedReturns -ne '0x00000001' -or $markStateReads -ne '0x00000001' -or ($markedResult -ne '0x00000000' -and $markedResult -ne '0x00000001')) { throw "C011EC13 non-null path did not complete exactly one marked(old_o) read in $name." }
                $postQueueOutcome = 'A'
            }
            Assert-Text $postQueueLine 'objectHeaderReads=00000000 methodTableReads=00000000 segmentReads=00000000 regionReads=00000000' "no diagnostic metadata reads added around the decision"
            $decisionReturnAddress = Get-MarkerField $postQueueLine 'decisionReturnAddress'
            $safeStopAddress = Get-MarkerField $postQueueLine 'safeStopAddress'
            $nextMutationAddress = Get-MarkerField $postQueueLine 'nextMutationAddress'
            if ([string]::IsNullOrWhiteSpace($decisionReturnAddress) -or [string]::IsNullOrWhiteSpace($safeStopAddress) -or [string]::IsNullOrWhiteSpace($nextMutationAddress)) { throw "C011EC13 did not record the machine-code decision boundary in $name." }
            $machineCodeEvidencePath = Join-Path $runRoot 'post-queue-machine-code.txt'
            if (-not (Test-Path -LiteralPath $machineCodeEvidencePath -PathType Leaf)) {
                Set-Content -LiteralPath $machineCodeEvidencePath -Value @(
                    "markHelperSymbol=$markHelperSymbolName",
                    "markHelperAddress=0x$markHelperAddressText",
                    "decisionReturnAddress=$decisionReturnAddress",
                    "safeStopAddress=$safeStopAddress",
                    "nextMutationAddress=$nextMutationAddress",
                    "source=locked gc.cpp:27308-27335 queue_mark; old_o capture, queue slot write, cursor advance, old_o null branch, marked(old_o) only on the non-null branch",
                    "machineCode=artifact-disassembly.txt",
                    "safeStop=after the real null/marked decision and before return/callback continuation or any later mutation"
                ) -Encoding ASCII
            }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker="C011EC13"; outcome=$postQueueOutcome; harnessTerminated=$true
                root=[ordered]@{slot=(Get-MarkerField $postQueueLine 'rootSlot');rawValue=$postQueueRoot;storageObject=$postQueueStorage;sentinel=$postQueueSentinel}
                queue=[ordered]@{slotAddress=(Get-MarkerField $postQueueLine 'selectedSlot');slotIndex=(Get-MarkerField $postQueueLine 'slotIndex');oldSlot=$postQueueSlotOld;newSlot=$postQueueNew;oldObject=$postQueueOld;cursorBefore=(Get-MarkerField $postQueueLine 'cursorBefore');cursorAfter=(Get-MarkerField $postQueueLine 'cursorAfter');queueBase=(Get-MarkerField $postQueueLine 'queueBase')}
                decision=[ordered]@{requests=(Get-MarkerField $postQueueLine 'decisionRequests');entries=(Get-MarkerField $postQueueLine 'decisionEntries');completions=(Get-MarkerField $postQueueLine 'decisionCompletions');duplicates=(Get-MarkerField $postQueueLine 'decisionDuplicates');nullTests=(Get-MarkerField $postQueueLine 'nullTests');nullResult=$nullResult;branch=(Get-MarkerField $postQueueLine 'branch');markedRequests=$markedRequests;markedEntries=$markedEntries;markedReturns=$markedReturns;markedResult=$markedResult;markStateReads=$markStateReads;decisionReturnAddress=$decisionReturnAddress;safeStopAddress=$safeStopAddress;nextMutationAddress=$nextMutationAddress}
                mutation=[ordered]@{inheritedQueueSlotWrites=(Get-MarkerField $postQueueLine 'inheritedQueueSlotWrites');inheritedCursorWrites=(Get-MarkerField $postQueueLine 'inheritedCursorWrites');newAttempts=(Get-MarkerField $postQueueLine 'newMutationAttempts');newExecutions=(Get-MarkerField $postQueueLine 'newMutationExecutions');markBitWrites=(Get-MarkerField $postQueueLine 'markBitWrites')}
                traversal=[ordered]@{logicalMarkComplete=(Get-MarkerField $postQueueLine 'logicalMarkComplete');scheduled=(Get-MarkerField $postQueueLine 'traversalScheduled');graph=(Get-MarkerField $postQueueLine 'graphTraversal');childReads=(Get-MarkerField $postQueueLine 'childReferenceReads');childObjects=(Get-MarkerField $postQueueLine 'childObjectsDiscovered');secondObjectAttempts=(Get-MarkerField $postQueueLine 'secondObjectMarkAttempts')}
                threadStore=[ordered]@{lockHeld=(Get-MarkerField $postQueueLine 'lockHeld');eeSuspended=(Get-MarkerField $postQueueLine 'eeSuspended');managedEntryProhibited=(Get-MarkerField $postQueueLine 'managedEntryProhibited');registeredThreads=(Get-MarkerField $postQueueLine 'registeredThreads');enumeratedThreads=(Get-MarkerField $postQueueLine 'enumeratedThreads');includedThreads=(Get-MarkerField $postQueueLine 'includedThreads');registryMutation=(Get-MarkerField $postQueueLine 'registryMutation');restart=(Get-MarkerField $postQueueLine 'restart');resume=(Get-MarkerField $postQueueLine 'resume')}
                validation=[ordered]@{sentinelFailures=(Get-MarkerField $postQueueLine 'sentinelFailures');objectValidationBeforeFixup=(Get-MarkerField $postQueueLine 'objectValidationBeforeFixup');objectValidationAfterFixup=(Get-MarkerField $postQueueLine 'objectValidationAfterFixup');duplicateObjectAddresses=(Get-MarkerField $postQueueLine 'duplicateObjectAddresses');objectHistoryOverflow=(Get-MarkerField $postQueueLine 'objectHistoryOverflow')}
            }
        } elseif ($isFirstRootFirstMarkMutation) {
            Assert-Text $validationText '\[nativeaot-gc-first-root-first-mark-mutation\] SAFE_STOP marker=C011EC12' "first real root first-mark mutation marker"
            $firstMarkLine = (($validationText -split "`n") | Where-Object { $_ -match '\[nativeaot-gc-first-root-first-mark-mutation\] SAFE_STOP marker=C011EC12' } | Select-Object -Last 1)
            if ([string]::IsNullOrWhiteSpace($firstMarkLine)) { throw "C011EC12 marker line was not isolated in $name." }
            Assert-Text $firstMarkLine 'membership=00000001 wksMultipleHeaps=00000000 hpt=0000000000000000 heapOf=0000000000000000 heapNumber=00000000 heapCount=00000001 wksNullHeapValid=00000001 condemned=00000001' "membership, WKS null heap, and condemned=true"
            Assert-Text $firstMarkLine 'markCallAttempts=00000001 markCalls=00000001 duplicateMarkCalls=00000000' "one authentic mark helper request and entry"
            Assert-Text $firstMarkLine 'firstMutationAttempts=00000001 firstMutationExecutions=00000001 secondMutationAttempts=00000000 secondMutationExecutions=00000000' "exactly one first mutation and no second mutation"
            Assert-Text $firstMarkLine 'mutationKind=queue_slot_and_cursor_atomic_unit' "queue mutation semantic type"
            Assert-Text $firstMarkLine 'worklistOld=0000000000000000 .*slotIndexBefore=0000000000000000 cursorBefore=0000000000000000 slotIndexAfter=0000000000000000 cursorAfter=0000000000000001 capacity=0000000000000010' "queue constructor state and cursor advancement"
            Assert-Text $firstMarkLine 'markStateReads=00000000 markStateResult=00000000 markBitWrites=00000000 worklistSlotWrites=00000001 worklistCursorWrites=00000001 objectHeaderWrites=00000000 gcMetadataWrites=00000000 segmentWrites=00000000' "queue writes before mark-state read"
            Assert-Text $firstMarkLine 'logicalMarkComplete=00000000 traversalScheduled=00000000 graphTraversal=00000000 childReferenceReads=00000000 childObjectsDiscovered=00000000 secondObjectMarkAttempts=00000000' "no logical mark completion or traversal"
            Assert-Text $firstMarkLine 'promotionStart=00000000 promotions=00000000 promotionWrites=00000000 callbackReturns=00000000 secondCallbacks=00000000 restart=00000000 resume=00000000 markHelperReturns=00000000' "no continuation beyond safe stop"
            Assert-Text $firstMarkLine 'lockHeld=00000001 eeSuspended=00000001 managedEntryProhibited=00000001 registeredThreads=00000001 enumeratedThreads=00000001 includedThreads=00000001 registryMutation=00000000 allocationContextsCleared=00000001' "suspended one-mutator invariants"
            Assert-Text $firstMarkLine 'sentinelFailures=00000000 objectValidationBeforeFixup=00000000 objectValidationAfterFixup=00000000 objectOverlapFailures=00000000 objectPatternFailures=00000000 duplicateObjectAddresses=00000000 objectHistoryOverflow=00000000' "object and sentinel validation"
            $firstMarkRootSlot = Get-MarkerField $firstMarkLine 'rootSlot'
            $firstMarkRawRoot = Get-MarkerField $firstMarkLine 'rawRoot'
            $firstMarkStorage = Get-MarkerField $firstMarkLine 'storageObject'
            $firstMarkSentinel = Get-MarkerField $firstMarkLine 'sentinel'
            if ($firstMarkRawRoot -ne $firstMarkStorage -or $firstMarkRawRoot -eq $firstMarkSentinel) { throw "C011EC12 root identity did not remain the storage object in $name." }
            $firstMarkNew = Get-MarkerField $firstMarkLine 'worklistNew'
            if ($firstMarkNew -ne $firstMarkStorage) { throw "C011EC12 worklist write did not directly encode the storage object in $name." }
            $firstMarkTarget = Get-MarkerField $firstMarkLine 'worklistTarget'
            $firstMarkQueueBase = Get-MarkerField $firstMarkLine 'queueBase'
            if ($firstMarkTarget -ne $firstMarkQueueBase) { throw "C011EC12 slot-zero target did not equal queue base in $name." }
            $machineCodeEvidencePath = Join-Path $runRoot 'first-mark-machine-code.txt'
            Require-File $machineCodeEvidencePath "C011EC12 machine-code evidence"
            $machineCodeEvidence = Get-Content -LiteralPath $machineCodeEvidencePath -Raw
            $firstMarkInstructionMatch = [regex]::Match($machineCodeEvidence, '(?im)^firstMutationInstruction=(?<value>0x[0-9A-F]+)')
            $firstMarkNextInstructionMatch = [regex]::Match($machineCodeEvidence, '(?im)^nextMutationInstruction=(?<value>0x[0-9A-F]+)')
            if (-not $firstMarkInstructionMatch.Success -or -not $firstMarkNextInstructionMatch.Success) { throw "C011EC12 did not record both exact machine-code mutation boundaries in $name." }
            $firstMarkInstruction = $firstMarkInstructionMatch.Groups['value'].Value
            $firstMarkNextInstruction = $firstMarkNextInstructionMatch.Groups['value'].Value
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker="C011EC12"; outcome="D"; harnessTerminated=$true
                root=[ordered]@{slot=$firstMarkRootSlot;rawValue=$firstMarkRawRoot;storageObject=$firstMarkStorage;sentinel=$firstMarkSentinel;membership=(Get-MarkerField $firstMarkLine 'membership')}
                heap=[ordered]@{multipleHeaps=(Get-MarkerField $firstMarkLine 'wksMultipleHeaps');hpt=(Get-MarkerField $firstMarkLine 'hpt');heapOf=(Get-MarkerField $firstMarkLine 'heapOf');heapNumber=(Get-MarkerField $firstMarkLine 'heapNumber');heapCount=(Get-MarkerField $firstMarkLine 'heapCount');nullSentinelValid=(Get-MarkerField $firstMarkLine 'wksNullHeapValid')}
                condemned=[ordered]@{result=(Get-MarkerField $firstMarkLine 'condemned');generation=(Get-MarkerField $firstMarkLine 'generationFromRegion');condemnedGeneration=(Get-MarkerField $firstMarkLine 'condemnedGeneration');maximumGeneration=(Get-MarkerField $firstMarkLine 'maximumGeneration')}
                markHelper=[ordered]@{requests=(Get-MarkerField $firstMarkLine 'markCallAttempts');entries=(Get-MarkerField $firstMarkLine 'markCalls');duplicates=(Get-MarkerField $firstMarkLine 'duplicateMarkCalls');po=(Get-MarkerField $firstMarkLine 'helperPo');object=(Get-MarkerField $firstMarkLine 'helperObject')}
                mutation=[ordered]@{attempts=(Get-MarkerField $firstMarkLine 'firstMutationAttempts');executions=(Get-MarkerField $firstMarkLine 'firstMutationExecutions');secondAttempts=(Get-MarkerField $firstMarkLine 'secondMutationAttempts');secondExecutions=(Get-MarkerField $firstMarkLine 'secondMutationExecutions');kind="queue_slot_and_cursor_atomic_unit";target=$firstMarkTarget;old=(Get-MarkerField $firstMarkLine 'worklistOld');new=$firstMarkNew;queueBase=$firstMarkQueueBase;slotIndexBefore=(Get-MarkerField $firstMarkLine 'slotIndexBefore');cursorBefore=(Get-MarkerField $firstMarkLine 'cursorBefore');slotIndexAfter=(Get-MarkerField $firstMarkLine 'slotIndexAfter');cursorAfter=(Get-MarkerField $firstMarkLine 'cursorAfter');capacity=(Get-MarkerField $firstMarkLine 'capacity');firstInstruction=$firstMarkInstruction;nextInstruction=$firstMarkNextInstruction}
                counters=[ordered]@{worklistMetadataReads=(Get-MarkerField $firstMarkLine 'worklistMetadataReads');markStateReads=(Get-MarkerField $firstMarkLine 'markStateReads');markBitWrites=(Get-MarkerField $firstMarkLine 'markBitWrites');worklistSlotWrites=(Get-MarkerField $firstMarkLine 'worklistSlotWrites');worklistCursorWrites=(Get-MarkerField $firstMarkLine 'worklistCursorWrites');objectHeaderWrites=(Get-MarkerField $firstMarkLine 'objectHeaderWrites');gcMetadataWrites=(Get-MarkerField $firstMarkLine 'gcMetadataWrites');segmentWrites=(Get-MarkerField $firstMarkLine 'segmentWrites')}
                traversal=[ordered]@{logicalMarkComplete=(Get-MarkerField $firstMarkLine 'logicalMarkComplete');scheduled=(Get-MarkerField $firstMarkLine 'traversalScheduled');graph=(Get-MarkerField $firstMarkLine 'graphTraversal');childReads=(Get-MarkerField $firstMarkLine 'childReferenceReads');childObjects=(Get-MarkerField $firstMarkLine 'childObjectsDiscovered');secondObjectAttempts=(Get-MarkerField $firstMarkLine 'secondObjectMarkAttempts')}
                threadStore=[ordered]@{lockHeld=(Get-MarkerField $firstMarkLine 'lockHeld');eeSuspended=(Get-MarkerField $firstMarkLine 'eeSuspended');managedEntryProhibited=(Get-MarkerField $firstMarkLine 'managedEntryProhibited');registeredThreads=(Get-MarkerField $firstMarkLine 'registeredThreads');enumeratedThreads=(Get-MarkerField $firstMarkLine 'enumeratedThreads');includedThreads=(Get-MarkerField $firstMarkLine 'includedThreads');registryMutation=(Get-MarkerField $firstMarkLine 'registryMutation');restart=(Get-MarkerField $firstMarkLine 'restart');resume=(Get-MarkerField $firstMarkLine 'resume')}
                validation=[ordered]@{sentinelChecks=(Get-MarkerField $firstMarkLine 'sentinelChecks');sentinelFailures=(Get-MarkerField $firstMarkLine 'sentinelFailures');objectValidationBeforeFixup=(Get-MarkerField $firstMarkLine 'objectValidationBeforeFixup');objectValidationAfterFixup=(Get-MarkerField $firstMarkLine 'objectValidationAfterFixup');objectOverlapFailures=(Get-MarkerField $firstMarkLine 'objectOverlapFailures');objectPatternFailures=(Get-MarkerField $firstMarkLine 'objectPatternFailures');duplicateObjectAddresses=(Get-MarkerField $firstMarkLine 'duplicateObjectAddresses');objectHistoryOverflow=(Get-MarkerField $firstMarkLine 'objectHistoryOverflow')}
            }
        } elseif ($isFirstRootPreMarkBoundary) {
            Assert-Text $validationText '\[nativeaot-gc-first-root-pre-mark-boundary\] SAFE_STOP marker=C011EC11' "first real root pre-mark boundary marker"
            $preMarkLine = (($validationText -split "`n") | Where-Object { $_ -match '\[nativeaot-gc-first-root-pre-mark-boundary\] SAFE_STOP marker=C011EC11' } | Select-Object -Last 1)
            if ([string]::IsNullOrWhiteSpace($preMarkLine)) { throw "C011EC11 marker line was not isolated in $name." }
            Assert-Text $preMarkLine 'membership=00000001 wksMultipleHeaps=00000000 hpt=0000000000000000 heapOf=0000000000000000 heapNumber=00000000 heapCount=00000001 wksNullHeapValid=00000001 condemned=00000001' "true membership, valid Workstation null heap, and condemned=true"
            Assert-Text $preMarkLine 'trueBranchRequests=00000001 trueBranchEntries=00000001 trueBranchDuplicates=00000000' "one true-branch traversal"
            Assert-Text $preMarkLine 'dprintfCompiled=00000000 dprintfRequests=00000000 dprintfEntries=00000000 dprintfReturns=00000000' "release-build dprintf elimination"
            Assert-Text $preMarkLine 'rawFlags=00000000 flagTests=00000002 interiorFlag=00000000 pinnedFlag=00000000' "source-defined root flag tests"
            Assert-Text $preMarkLine 'conservativeChecks=00000001 .*objectIsFree=00000000 .*debugValidationEntries=00000001 .*debugValidationCompletions=00000001' "conservative and debug-only gates"
            Assert-Text $preMarkLine 'markStateReads=00000000 markStateResult=00000000' "no mark-state read"
            Assert-Text $preMarkLine 'markCallAttempts=00000000 markCalls=00000000' "no mark helper execution"
            Assert-Text $preMarkLine 'promotionStart=00000000 promotions=00000000 promotionWrites=00000000 markWrites=00000000 worklistWrites=00000000 graphTraversal=00000000 childReferenceReads=00000000 objectMutation=00000000 objectHeaderWrites=00000000 gcMetadataMutation=00000000 segmentMutation=00000000 relocationWrites=00000000' "zero mutation counters"
            Assert-Text $preMarkLine 'callbackReturns=00000000 secondCallbacks=00000000 restart=00000000 resume=00000000' "zero callback continuation and restart"
            Assert-Text $preMarkLine 'lockHeld=00000001 eeSuspended=00000001 managedEntryProhibited=00000001 registeredThreads=00000001 enumeratedThreads=00000001 includedThreads=00000001 registryMutation=00000000 allocationContextsCleared=00000001' "suspended one-mutator invariants"
            Assert-Text $validationText 'contextFieldReads=00000006 .*contextPromotion=00000001 contextConcurrent=00000000' "source-required ScanContext observation"
            $preMarkRootSlot = Get-MarkerField $preMarkLine 'rootSlot'
            $preMarkRawRoot = Get-MarkerField $preMarkLine 'rawRoot'
            $preMarkStorage = Get-MarkerField $preMarkLine 'storageObject'
            $preMarkSentinel = Get-MarkerField $preMarkLine 'sentinel'
            $preMarkHeap = Get-MarkerField $preMarkLine 'heapOf'
            if ($preMarkRawRoot -ne $preMarkStorage) { throw "C011EC11 raw root did not equal the genuine NativeAOT thread-static storage object in $name." }
            if ($preMarkRawRoot -eq $preMarkSentinel) { throw "C011EC11 treated the proof sentinel as the collected root in $name." }
            $preMarkCondemnedGeneration = Get-MarkerField $preMarkLine 'condemnedGeneration'
            $preMarkMaximumGeneration = Get-MarkerField $preMarkLine 'maximumGeneration'
            $preMarkGeneration = Get-MarkerField $preMarkLine 'generationFromRegion'
            if ($preMarkCondemnedGeneration -ne '0x00000000' -or $preMarkMaximumGeneration -ne '0x00000002' -or $preMarkGeneration -ne '0x00000000') { throw "C011EC11 condemned-generation values diverged from C011EC10 in $name." }
            $preMarkHelper = Get-MarkerField $preMarkLine 'markHelper'
            $preMarkBoundaryReturn = Get-MarkerField $preMarkLine 'boundaryReturn'
            if ([string]::IsNullOrWhiteSpace($preMarkHelper) -or [string]::IsNullOrWhiteSpace($preMarkBoundaryReturn)) { throw "C011EC11 did not record the poised mark helper and boundary return address in $name." }
            $preMarkObjectHeaders = Get-MarkerField $preMarkLine 'objectHeaders'
            $preMarkMethodTables = Get-MarkerField $preMarkLine 'methodTables'
            if ($preMarkObjectHeaders -ne $preMarkMethodTables -or $preMarkObjectHeaders -eq '0x00000000') { throw "C011EC11 did not record the required Validate method-table read in $name." }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker="C011EC11"; outcome="A"; harnessTerminated=$true
                root=[ordered]@{slot=$preMarkRootSlot;rawValue=$preMarkRawRoot;storageObject=$preMarkStorage;sentinel=$preMarkSentinel;membership=(Get-MarkerField $preMarkLine 'membership')}
                heap=[ordered]@{multipleHeaps=(Get-MarkerField $preMarkLine 'wksMultipleHeaps');hpt=(Get-MarkerField $preMarkLine 'hpt');heapOf=$preMarkHeap;heapNumber=(Get-MarkerField $preMarkLine 'heapNumber');heapCount=(Get-MarkerField $preMarkLine 'heapCount');nullSentinelValid=(Get-MarkerField $preMarkLine 'wksNullHeapValid')}
                condemned=[ordered]@{result=(Get-MarkerField $preMarkLine 'condemned');generation=$preMarkGeneration;condemnedGeneration=$preMarkCondemnedGeneration;maximumGeneration=$preMarkMaximumGeneration;generationTableReads=(Get-MarkerField $preMarkLine 'generationTableReads');segmentLookups=(Get-MarkerField $preMarkLine 'generationSegmentLookups')}
                trueBranch=[ordered]@{requests=(Get-MarkerField $preMarkLine 'trueBranchRequests');entries=(Get-MarkerField $preMarkLine 'trueBranchEntries');duplicates=(Get-MarkerField $preMarkLine 'trueBranchDuplicates')}
                dprintf=[ordered]@{compiled=(Get-MarkerField $preMarkLine 'dprintfCompiled');requests=(Get-MarkerField $preMarkLine 'dprintfRequests');entries=(Get-MarkerField $preMarkLine 'dprintfEntries');returns=(Get-MarkerField $preMarkLine 'dprintfReturns')}
                flags=[ordered]@{raw=(Get-MarkerField $preMarkLine 'rawFlags');tests=(Get-MarkerField $preMarkLine 'flagTests');interior=(Get-MarkerField $preMarkLine 'interiorFlag');pinned=(Get-MarkerField $preMarkLine 'pinnedFlag')}
                metadata=[ordered]@{objectHeaders=$preMarkObjectHeaders;methodTables=$preMarkMethodTables;firstReadAddress=(Get-MarkerField $preMarkLine 'firstMetadataReadAddress');methodTable=(Get-MarkerField $preMarkLine 'methodTable');segmentReads=(Get-MarkerField $preMarkLine 'segmentReads');gcMetadataReads=(Get-MarkerField $preMarkLine 'gcMetadataReads');markStateReads=(Get-MarkerField $preMarkLine 'markStateReads');markStateResult=(Get-MarkerField $preMarkLine 'markStateResult')}
                mutation=[ordered]@{markHelper=$preMarkHelper;callSite=(Get-MarkerField $preMarkLine 'mutationCallSite');firstMutationInstruction=(Get-MarkerField $preMarkLine 'firstMutationInstruction');boundaryReturn=$preMarkBoundaryReturn;markAttempts=(Get-MarkerField $preMarkLine 'markCallAttempts');markCalls=(Get-MarkerField $preMarkLine 'markCalls');promotionStart=(Get-MarkerField $preMarkLine 'promotionStart');promotions=(Get-MarkerField $preMarkLine 'promotions');promotionWrites=(Get-MarkerField $preMarkLine 'promotionWrites');markWrites=(Get-MarkerField $preMarkLine 'markWrites');worklistWrites=(Get-MarkerField $preMarkLine 'worklistWrites');graphTraversal=(Get-MarkerField $preMarkLine 'graphTraversal');childReferenceReads=(Get-MarkerField $preMarkLine 'childReferenceReads');objectMutation=(Get-MarkerField $preMarkLine 'objectMutation');gcMetadataMutation=(Get-MarkerField $preMarkLine 'gcMetadataMutation');segmentMutation=(Get-MarkerField $preMarkLine 'segmentMutation');relocationWrites=(Get-MarkerField $preMarkLine 'relocationWrites')}
                threadStore=[ordered]@{managedThread=(Get-MarkerField $preMarkLine 'managedThread');currentThread=(Get-MarkerField $preMarkLine 'currentThread');enumeratedThread=(Get-MarkerField $preMarkLine 'enumeratedThread');lockOwner=(Get-MarkerField $preMarkLine 'lockOwner');lockHeld=(Get-MarkerField $preMarkLine 'lockHeld');eeSuspended=(Get-MarkerField $preMarkLine 'eeSuspended');managedEntryProhibited=(Get-MarkerField $preMarkLine 'managedEntryProhibited');registeredThreads=(Get-MarkerField $preMarkLine 'registeredThreads');enumeratedThreads=(Get-MarkerField $preMarkLine 'enumeratedThreads');includedThreads=(Get-MarkerField $preMarkLine 'includedThreads');registryMutation=(Get-MarkerField $preMarkLine 'registryMutation');restart=(Get-MarkerField $preMarkLine 'restart');resume=(Get-MarkerField $preMarkLine 'resume');allocationContextsCleared=(Get-MarkerField $preMarkLine 'allocationContextsCleared')}
                scanContext=[ordered]@{fieldReads=(Get-MarkerField $validationText 'contextFieldReads');promotion=(Get-MarkerField $validationText 'contextPromotion');concurrent=(Get-MarkerField $validationText 'contextConcurrent');threadCount=(Get-MarkerField $validationText 'contextThreadCount');threadNumber=(Get-MarkerField $validationText 'contextThreadNumber');threadUnderCrawl=(Get-MarkerField $validationText 'contextThread');stackLimit=(Get-MarkerField $validationText 'contextStackLimit')}
                validation=[ordered]@{sentinelChecks=(Get-MarkerField $preMarkLine 'sentinelChecks');objectHistoryOverflow=(Get-MarkerField $preMarkLine 'objectHistoryOverflow')}
            }
        } elseif ($isFirstRootCondemnedGenerationDecision) {
            Assert-Text $validationText '\[nativeaot-gc-first-root-condemned-generation-decision\] SAFE_STOP marker=C011EC10' "first real root condemned-generation safe-stop marker"
            Assert-Text $validationText 'callbackRequestCount=00000001 callbackCallSiteCount=00000001 callbackInvocationCount=00000001 callbackEntryCount=00000001 callbackReturnCount=00000000 secondCallbackAttempts=00000000' "exactly one real callback"
            Assert-Text $validationText 'membershipRequests=00000001 membershipEntries=00000001 membershipCompletions=00000001 membershipObjectDereferences=00000000' "one true membership prerequisite counters"
            Assert-Text $validationText 'inFindObjectRange=00000001' "true membership result"
            Assert-Text $validationText 'multipleHeaps=00000000 hpt=0000000000000000 heapOf=0000000000000000 heapNumber=00000000 heapCount=00000001 workstationSingleHeapSentinelValid=00000001 heapResolutionFailures=00000000' "valid Workstation single-heap null sentinel"
            Assert-Text $validationText 'condemnedRequests=00000001 condemnedEntries=00000001 condemnedCompletions=00000001 condemnedReturns=00000001 condemnedDuplicates=00000000' "exactly one condemned-generation decision"
            Assert-Text $validationText 'condemnedObjectDereferences=00000000 condemnedGenerationQueryStart=00000001 condemnedGenerationQueryCompletions=00000001 condemnedGenerationTableReads=00000001 condemnedSegmentLookups=00000001' "source-required generation and debug segment lookup"
            Assert-Text $validationText 'condemnedObjectHeaders=00000000 condemnedMethodTables=00000000' "zero condemned-helper object metadata reads"
            Assert-Text $validationText 'objectHeaders=00000000 methodTables=00000000 childReferenceReads=00000000 promotionStart=00000000 promotions=00000000' "zero promotion and object reads"
            Assert-Text $validationText 'markingStart=00000000 markingWrites=00000000 graphTraversal=00000000 promotionWrites=00000000 objectMutation=00000000 gcMetadataMutation=00000000 segmentMutation=00000000' "zero marking, traversal, and mutation"
            Assert-Text $validationText 'managedAssignmentCount=00000001 managedClearCount=00000000 managedReadbackCount=00000001 managedAssignmentValid=00000001 managedReadbackValid=00000001' "managed ThreadStatic proof"
            Assert-Text $validationText 'duplicateObjectAddresses=00000000 objectHistoryOverflow=00000000' "object and duplicate validation"
            Assert-Text $validationText 'lockHeld=00000001 eeSuspended=00000001 managedEntryProhibited=00000001 callbackReturns=00000000 secondCallbacks=00000000 restartRequests=00000000 restartEntries=00000000 managedResume=00000000' "suspended single-mutator stop"
            Assert-Text $validationText 'lockDepth=00000001 registeredManagedThreads=00000001 currentThreadRegistered=00000001 currentThreadIsInitiator=00000001 currentAndInitiatorMatch=00000001 enumeratedThreads=00000001 includedThreads=00000001 duplicateThreads=00000000 allocationContextsVisited=00000001 allocationContextsChanged=00000001 allocationContextsCleared=00000001' "thread and fixup invariants"
            $callbackLoadedRoot = Get-MarkerField $validationText 'callbackLoadedRoot'
            $condemnedObject = Get-MarkerField $validationText 'condemnedCheckObject'
            $membershipObject = Get-MarkerField $validationText 'membershipObject'
            $heapObject = Get-MarkerField $validationText 'heapResolutionObject'
            $storageObject = Get-MarkerField $validationText 'storageObject'
            $rootSlot = Get-MarkerField $validationText 'rootSlot'
            $membershipLower = Get-MarkerField $validationText 'membershipLowerBound'
            $membershipUpper = Get-MarkerField $validationText 'membershipUpperBound'
            if ($callbackLoadedRoot -ne $condemnedObject -or $condemnedObject -ne $membershipObject -or $membershipObject -ne $heapObject -or $heapObject -ne $storageObject -or (Get-MarkerField $validationText 'callbackRootMatches') -ne '0x00000001' -or (Get-MarkerField $validationText 'membershipMatches') -ne '0x00000001' -or (Get-MarkerField $validationText 'heapResolutionMatches') -ne '0x00000001') { throw "Condemned-check input did not equal the genuine callback root in $name." }
            $objectValue = [Convert]::ToUInt64($condemnedObject.Substring(2), 16)
            $lowerValue = [Convert]::ToUInt64($membershipLower.Substring(2), 16)
            $upperValue = [Convert]::ToUInt64($membershipUpper.Substring(2), 16)
            if ($lowerValue -gt $objectValue -or $objectValue -ge $upperValue) { throw "Condemned-check root was outside the recorded membership bounds in $name." }
            $condemnedResult = Get-MarkerField $validationText 'condemnedResult'
            $generation = Get-MarkerField $validationText 'generationFromRegion'
            $condemnedGeneration = Get-MarkerField $validationText 'condemnedGeneration'
            $maximumGeneration = Get-MarkerField $validationText 'maximumGeneration'
            $expectedResult = if ([Convert]::ToUInt64($condemnedGeneration.Substring(2),16) -eq [Convert]::ToUInt64($maximumGeneration.Substring(2),16) -or [Convert]::ToUInt64($generation.Substring(2),16) -le [Convert]::ToUInt64($condemnedGeneration.Substring(2),16)) { '0x00000001' } else { '0x00000000' }
            if ($condemnedResult -ne $expectedResult) { throw "Condemned-check result did not match the locked generation comparison in $name." }
            $nextSourceOperation = if ($condemnedResult -eq '0x00000001') { 'GCHeap::Promote true branch dprintf @ gc.cpp:49507' } else { 'GCHeap::Promote false branch return @ gc.cpp:49504' }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker="C011EC10"; outcome=if ($condemnedResult -eq '0x00000001') { "A" } else { "B" }; harnessTerminated=$true
                callbackRequestCount=(Get-MarkerField $validationText 'callbackRequestCount'); callbackEntryCount=(Get-MarkerField $validationText 'callbackEntryCount'); callbackReturnCount=(Get-MarkerField $validationText 'callbackReturnCount'); duplicateCallbacks=(Get-MarkerField $validationText 'secondCallbackAttempts')
                root=[ordered]@{slot=$rootSlot;rawValue=$callbackLoadedRoot;condemnedObject=$condemnedObject;storageObject=$storageObject;sentinel=(Get-MarkerField $validationText 'sentinelAddress');sentinelReadback=(Get-MarkerField $validationText 'sentinelReadback')}
                membership=[ordered]@{object=$membershipObject;lowerBound=$membershipLower;upperBound=$membershipUpper;result=(Get-MarkerField $validationText 'inFindObjectRange')}
                heapResolution=[ordered]@{input=$heapObject;hpt=(Get-MarkerField $validationText 'hpt');heap=(Get-MarkerField $validationText 'heapOf');heapNumber=(Get-MarkerField $validationText 'heapNumber');heapCount=(Get-MarkerField $validationText 'heapCount');multipleHeaps=(Get-MarkerField $validationText 'multipleHeaps');singleHeapSentinelValid=(Get-MarkerField $validationText 'workstationSingleHeapSentinelValid');failures=(Get-MarkerField $validationText 'heapResolutionFailures')}
                condemnedCheck=[ordered]@{requests=(Get-MarkerField $validationText 'condemnedRequests');entries=(Get-MarkerField $validationText 'condemnedEntries');completions=(Get-MarkerField $validationText 'condemnedCompletions');returns=(Get-MarkerField $validationText 'condemnedReturns');duplicates=(Get-MarkerField $validationText 'condemnedDuplicates');object=$condemnedObject;lowerBound=(Get-MarkerField $validationText 'condemnedLowerBound');upperBound=(Get-MarkerField $validationText 'condemnedUpperBound');condemnedGeneration=$condemnedGeneration;maximumGeneration=$maximumGeneration;generation=$generation;generationTable=(Get-MarkerField $validationText 'generationTable');generationTableIndex=(Get-MarkerField $validationText 'generationTableIndex');segment=(Get-MarkerField $validationText 'condemnedSegmentIdentity');segmentLookups=(Get-MarkerField $validationText 'condemnedSegmentLookups');result=$condemnedResult;branch=(Get-MarkerField $validationText 'condemnedBranch');nextSourceOperation=$nextSourceOperation}
                counters=[ordered]@{generationQuery=(Get-MarkerField $validationText 'condemnedGenerationQueryStart');generationTableReads=(Get-MarkerField $validationText 'condemnedGenerationTableReads');segmentLookup=(Get-MarkerField $validationText 'condemnedSegmentLookups');objectDereferences=(Get-MarkerField $validationText 'condemnedObjectDereferences');objectHeaders=(Get-MarkerField $validationText 'condemnedObjectHeaders');methodTables=(Get-MarkerField $validationText 'condemnedMethodTables');promotion=(Get-MarkerField $validationText 'promotions');marking=(Get-MarkerField $validationText 'markingStart');graph=(Get-MarkerField $validationText 'graphTraversal')}
                mutation=[ordered]@{mark=(Get-MarkerField $validationText 'markingWrites');promotion=(Get-MarkerField $validationText 'promotionWrites');object=(Get-MarkerField $validationText 'objectMutation');gc=(Get-MarkerField $validationText 'gcMetadataMutation');segment=(Get-MarkerField $validationText 'segmentMutation')}
                managedProofRoot=[ordered]@{assignmentCount=(Get-MarkerField $validationText 'managedAssignmentCount');clearCount=(Get-MarkerField $validationText 'managedClearCount');readbackCount=(Get-MarkerField $validationText 'managedReadbackCount');assignmentValid=(Get-MarkerField $validationText 'managedAssignmentValid');readbackValid=(Get-MarkerField $validationText 'managedReadbackValid');sentinelAddress=(Get-MarkerField $validationText 'sentinelAddress');sentinelReadback=(Get-MarkerField $validationText 'sentinelReadback');storageObject=$storageObject;objectBefore=(Get-MarkerField $validationText 'objectBefore');objectAfter=(Get-MarkerField $validationText 'objectAfter');objectAtStop=(Get-MarkerField $validationText 'objectAtStop');sentinelChecks=(Get-MarkerField $validationText 'sentinelChecks');objectHistoryOverflow=(Get-MarkerField $validationText 'objectHistoryOverflow')}
                threadStore=[ordered]@{managedThread=(Get-MarkerField $validationText 'managedThread');currentThread=(Get-MarkerField $validationText 'currentThread');lockOwner=(Get-MarkerField $validationText 'lockOwner');lockHeld=(Get-MarkerField $validationText 'lockHeld');eeSuspended=(Get-MarkerField $validationText 'eeSuspended');managedEntryProhibited=(Get-MarkerField $validationText 'managedEntryProhibited');lockDepth=(Get-MarkerField $validationText 'lockDepth');registeredManagedThreads=(Get-MarkerField $validationText 'registeredManagedThreads');enumeratedThreads=(Get-MarkerField $validationText 'enumeratedThreads');includedThreads=(Get-MarkerField $validationText 'includedThreads')}
                restartRequests=(Get-MarkerField $validationText 'restartRequests');restartEntries=(Get-MarkerField $validationText 'restartEntries');managedResume=(Get-MarkerField $validationText 'managedResume')
            }
        } elseif ($isFirstRootHeapResolution) {
            Assert-Text $validationText '\[nativeaot-gc-first-root-heap-resolution\] SAFE_STOP marker=C011EC09' "first real root heap-resolution safe-stop marker"
            Assert-Text $validationText 'callbackRequestCount=00000001 callbackCallSiteCount=00000001 callbackInvocationCount=00000001 callbackEntryCount=00000001 callbackReturnCount=00000000 secondCallbackAttempts=00000000' "exactly one real callback"
            Assert-Text $validationText 'membershipRequests=00000001 membershipEntries=00000001 membershipCompletions=00000001 membershipReturns=00000000 membershipObjectDereferences=00000000' "exactly one membership prerequisite"
            Assert-Text $validationText 'lowerResult=00000001 upperResult=00000001 inFindObjectRange=00000001' "true managed-range membership"
            Assert-Text $validationText 'heapResolutionRequests=00000001 heapResolutionEntries=00000001 heapResolutionCompletions=00000001 heapResolutionDuplicates=00000000' "exactly one real heap-resolution transition"
            Assert-Text $validationText 'heapNumber=00000000 totalHeapCount=00000001 threadNumber=00000000' "source-defined Workstation heap identity"
            Assert-Text $validationText 'objectAddressConsulted=00000000 threadStateConsulted=00000000 heapTableReads=00000000 heapTableIdentity=0000000000000000 heapTableSlot=0000000000000000 segmentMapReads=00000000 segmentIdentity=0000000000000000 brickCardReads=00000000 rangeReads=00000000' "heap_of source contract has no object/table/segment bookkeeping reads"
            Assert-Text $validationText 'candidateClassification=00000000 generationClassificationStart=00000000 generationQueryStart=00000000 condemnedGenerationComparisons=00000000 ephemeralGenerationComparisons=00000000' "stopped before generation logic"
            Assert-Text $validationText 'postResolutionSegmentLookup=00000000 objectHeaders=00000000 methodTables=00000000 childReferenceReads=00000000 promotionStart=00000000 promotions=00000000 markingStart=00000000 graphTraversal=00000000' "zero post-resolution processing"
            Assert-Text $validationText 'markWrites=00000000 promotionWrites=00000000 objectMutation=00000000 gcMetadataMutation=00000000 segmentMetadataMutation=00000000' "zero mutation"
            Assert-Text $validationText 'managedAssignmentCount=00000001 managedClearCount=00000000 managedReadbackCount=00000001 managedAssignmentValid=00000001 managedReadbackValid=00000001' "managed ThreadStatic proof"
            Assert-Text $validationText 'objectHistoryOverflow=00000000 duplicateObjectAddresses=00000000' "object and duplicate validation"
            Assert-Text $validationText 'lockHeld=00000001 eeSuspended=00000001 managedEntryProhibited=00000001' "suspension invariant"
            Assert-Text $validationText 'managedThread=[0-9A-F]{16} currentThread=[0-9A-F]{16} lockOwner=[0-9A-F]{16}' "thread identity evidence"
            Assert-Text $validationText 'callbackContext=[0-9A-F]{16} contextFieldReads=00000006 contextThread=[0-9A-F]{16} contextStackLimit=0000000000000000 contextThreadNumber=0000000000000000 contextThreadCount=0000000000000001 contextPromotion=00000001 contextConcurrent=00000000' "live ScanContext"
            Assert-Text $validationText 'lockDepth=00000001 registeredManagedThreads=00000001 currentThreadRegistered=00000001 currentThreadIsInitiator=00000001 currentAndInitiatorMatch=00000001' "thread-store lock and initiator state"
            Assert-Text $validationText 'registeredThreadCountBeforeRoot=00000001 registeredThreadCountAfterRoot=00000001 enumeratedThreads=00000001 includedThreads=00000001 excludedThreads=00000000 duplicateThreads=00000000 registryMutationBefore=00000000 registryMutationAfter=00000000' "thread enumeration and registry invariants"
            Assert-Text $validationText 'allocationContextsVisited=00000001 allocationContextsChanged=00000001 allocationContextsCleared=00000001' "allocation-context fixup state"
            $managedThread = Get-MarkerField $validationText 'managedThread'
            $currentThread = Get-MarkerField $validationText 'currentThread'
            $lockOwner = Get-MarkerField $validationText 'lockOwner'
            if ($managedThread -ne $currentThread -or $currentThread -ne $lockOwner) { throw "Thread-store identities diverged in $name." }
            Assert-Text $validationText 'restartRequests=00000000 restartEntries=00000000 managedResume=00000000' "zero restart and resume"
            $callbackLoadedRoot = Get-MarkerField $validationText 'callbackLoadedRoot'
            $membershipObject = Get-MarkerField $validationText 'membershipObject'
            $heapObject = Get-MarkerField $validationText 'heapResolutionObject'
            $storageObject = Get-MarkerField $validationText 'storageObject'
            if ($callbackLoadedRoot -ne $membershipObject -or $membershipObject -ne $heapObject -or $heapObject -ne $storageObject -or (Get-MarkerField $validationText 'objectMatchesCallbackRoot') -ne '0x00000001' -or (Get-MarkerField $validationText 'objectMatchesMembershipObject') -ne '0x00000001') { throw "Heap-resolution input did not equal the genuine callback root in $name." }
            $heapSucceeded = Get-MarkerField $validationText 'heapResolutionSucceeded'
            $heapFailures = Get-MarkerField $validationText 'heapResolutionFailures'
            $resolvedHeap = Get-MarkerField $validationText 'resolvedHeap'
            if ($heapSucceeded -eq '0x00000001') {
                if ($heapFailures -ne '0x00000000' -or $resolvedHeap -eq '0x0000000000000000') { throw "Successful heap resolution had inconsistent result fields in $name." }
            } else {
                if ($heapFailures -ne '0x00000001' -or $resolvedHeap -ne '0x0000000000000000') { throw "Failed heap resolution did not stop with the source result in $name." }
            }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker="C011EC09"; outcome=if ($heapSucceeded -eq '0x00000001') { "A" } else { "B" }; harnessTerminated=$true
                callbackRequestCount=(Get-MarkerField $validationText 'callbackRequestCount'); callbackEntryCount=(Get-MarkerField $validationText 'callbackEntryCount'); callbackReturnCount=(Get-MarkerField $validationText 'callbackReturnCount'); duplicateCallbacks=(Get-MarkerField $validationText 'secondCallbackAttempts')
                root=[ordered]@{slot=(Get-MarkerField $validationText 'rootSlot'); loaded=$callbackLoadedRoot; storage=$storageObject; sentinel=(Get-MarkerField $validationText 'sentinelAddress')}
                membership=[ordered]@{object=$membershipObject; lowerBound=(Get-MarkerField $validationText 'lowerBound'); upperBound=(Get-MarkerField $validationText 'upperBound'); result=(Get-MarkerField $validationText 'inFindObjectRange'); requests=(Get-MarkerField $validationText 'membershipRequests'); entries=(Get-MarkerField $validationText 'membershipEntries'); completions=(Get-MarkerField $validationText 'membershipCompletions')}
                heapResolution=[ordered]@{requests=(Get-MarkerField $validationText 'heapResolutionRequests'); entries=(Get-MarkerField $validationText 'heapResolutionEntries'); completions=(Get-MarkerField $validationText 'heapResolutionCompletions'); duplicates=(Get-MarkerField $validationText 'heapResolutionDuplicates'); failures=$heapFailures; succeeded=$heapSucceeded; input=$heapObject; pointer=$resolvedHeap; heapNumber=(Get-MarkerField $validationText 'heapNumber'); totalHeapCount=(Get-MarkerField $validationText 'totalHeapCount'); threadHeap=(Get-MarkerField $validationText 'heapResolutionThreadHeap'); heapTableReads=(Get-MarkerField $validationText 'heapTableReads'); heapTableIdentity=(Get-MarkerField $validationText 'heapTableIdentity'); heapTableSlot=(Get-MarkerField $validationText 'heapTableSlot'); segmentMapReads=(Get-MarkerField $validationText 'segmentMapReads'); segment=(Get-MarkerField $validationText 'segmentIdentity'); failureReason=(Get-MarkerField $validationText 'failureReason'); completionReturnAddress=(Get-MarkerField $validationText 'heapResolutionCompletionReturnAddress')}
                counters=[ordered]@{generationClassification=(Get-MarkerField $validationText 'generationClassificationStart'); generationQuery=(Get-MarkerField $validationText 'generationQueryStart'); condemned=(Get-MarkerField $validationText 'condemnedGenerationComparisons'); ephemeral=(Get-MarkerField $validationText 'ephemeralGenerationComparisons'); segmentLookup=(Get-MarkerField $validationText 'postResolutionSegmentLookup'); headers=(Get-MarkerField $validationText 'objectHeaders'); methodTables=(Get-MarkerField $validationText 'methodTables'); promotion=(Get-MarkerField $validationText 'promotionStart'); marking=(Get-MarkerField $validationText 'markingStart'); graph=(Get-MarkerField $validationText 'graphTraversal')}
                mutation=[ordered]@{mark=(Get-MarkerField $validationText 'markWrites'); promotion=(Get-MarkerField $validationText 'promotionWrites'); object=(Get-MarkerField $validationText 'objectMutation'); gc=(Get-MarkerField $validationText 'gcMetadataMutation'); segment=(Get-MarkerField $validationText 'segmentMetadataMutation')}
                threadStore=[ordered]@{managedThread=$managedThread; currentThread=$currentThread; lockOwner=$lockOwner; lockHeld=(Get-MarkerField $validationText 'lockHeld'); lockDepth=(Get-MarkerField $validationText 'lockDepth'); registeredManagedThreads=(Get-MarkerField $validationText 'registeredManagedThreads'); currentThreadRegistered=(Get-MarkerField $validationText 'currentThreadRegistered'); currentThreadIsInitiator=(Get-MarkerField $validationText 'currentThreadIsInitiator'); currentAndInitiatorMatch=(Get-MarkerField $validationText 'currentAndInitiatorMatch'); registeredThreadCountBeforeRoot=(Get-MarkerField $validationText 'registeredThreadCountBeforeRoot'); registeredThreadCountAfterRoot=(Get-MarkerField $validationText 'registeredThreadCountAfterRoot'); enumeratedThreads=(Get-MarkerField $validationText 'enumeratedThreads'); includedThreads=(Get-MarkerField $validationText 'includedThreads'); excludedThreads=(Get-MarkerField $validationText 'excludedThreads'); duplicateThreads=(Get-MarkerField $validationText 'duplicateThreads'); registryMutationBefore=(Get-MarkerField $validationText 'registryMutationBefore'); registryMutationAfter=(Get-MarkerField $validationText 'registryMutationAfter'); eeSuspended=(Get-MarkerField $validationText 'eeSuspended'); managedEntryProhibited=(Get-MarkerField $validationText 'managedEntryProhibited')}
                allocationContextFixup=[ordered]@{visited=(Get-MarkerField $validationText 'allocationContextsVisited'); changed=(Get-MarkerField $validationText 'allocationContextsChanged'); cleared=(Get-MarkerField $validationText 'allocationContextsCleared')}
                scanContext=[ordered]@{address=(Get-MarkerField $validationText 'callbackContext'); fieldReads=(Get-MarkerField $validationText 'contextFieldReads'); thread=(Get-MarkerField $validationText 'contextThread'); stackLimit=(Get-MarkerField $validationText 'contextStackLimit'); promotion=(Get-MarkerField $validationText 'contextPromotion'); concurrent=(Get-MarkerField $validationText 'contextConcurrent'); threadCount=(Get-MarkerField $validationText 'contextThreadCount'); threadNumber=(Get-MarkerField $validationText 'contextThreadNumber')}
            }
        } elseif ($isFirstRootMembershipClassification) {
            Assert-Text $validationText '\[nativeaot-gc-first-root-membership-classification\] SAFE_STOP marker=C011EC08' "first real root membership safe-stop marker"
            Assert-Text $validationText 'callbackRequestCount=00000001'; Assert-Text $validationText 'callbackCallSiteCount=00000001'; Assert-Text $validationText 'callbackInvocationCount=00000001'; Assert-Text $validationText 'callbackEntryCount=00000001'; Assert-Text $validationText 'callbackReturnCount=00000000'; Assert-Text $validationText 'secondCallbackAttempts=00000000' "exactly one real callback"
            Assert-Text $validationText 'membershipRequests=00000001'; Assert-Text $validationText 'membershipEntries=00000001'; Assert-Text $validationText 'membershipCompletions=00000001'; Assert-Text $validationText 'membershipReturns=00000001'; Assert-Text $validationText 'duplicateChecks=00000000'; Assert-Text $validationText 'objectDereferences=00000000' "exactly one real membership check"
            Assert-Text $validationText 'lowerEvaluated=00000001'; Assert-Text $validationText 'upperEvaluated=00000001'; Assert-Text $validationText 'lowerResult=00000001'; Assert-Text $validationText 'upperResult=00000001'; Assert-Text $validationText 'inFindObjectRange=00000001'; Assert-Text $validationText 'sourceBranch=00000001' "true range comparisons and result"
            Assert-Text $validationText 'heapIdentity=0000000000000000'; Assert-Text $validationText 'heapFieldReads=00000001'; Assert-Text $validationText 'segmentIdentity=0000000000000000'; Assert-Text $validationText 'segmentLookupCount=00000000'; Assert-Text $validationText 'segmentLookupSucceeded=00000000' "one consulted per-heap boundary field and no segment lookup"
            Assert-Text $validationText 'candidateClassification=00000000'; Assert-Text $validationText 'generationClassificationStart=00000000'; Assert-Text $validationText 'generationQueryStart=00000000'; Assert-Text $validationText 'condemnedGenerationComparisons=00000000' "stopped before generation classification"
            Assert-Text $validationText 'postMembershipSegmentLookup=00000000'; Assert-Text $validationText 'objectHeaders=00000000'; Assert-Text $validationText 'methodTables=00000000'; Assert-Text $validationText 'promotionStart=00000000'; Assert-Text $validationText 'promotions=00000000'; Assert-Text $validationText 'markingStart=00000000'; Assert-Text $validationText 'graphTraversal=00000000' "zero post-membership processing"
            Assert-Text $validationText 'markWrites=00000000'; Assert-Text $validationText 'promotionWrites=00000000'; Assert-Text $validationText 'objectMutation=00000000'; Assert-Text $validationText 'gcMetadataMutation=00000000'; Assert-Text $validationText 'segmentMetadataMutation=00000000' "zero mutation"
            Assert-Text $validationText 'managedAssignmentCount=00000001'; Assert-Text $validationText 'managedClearCount=00000000'; Assert-Text $validationText 'managedReadbackCount=00000001'; Assert-Text $validationText 'managedAssignmentValid=00000001'; Assert-Text $validationText 'managedReadbackValid=00000001' "managed ThreadStatic proof"
            Assert-Text $validationText 'sentinelAddress=[0-9A-F]{16}'; Assert-Text $validationText 'sentinelReadback=[0-9A-F]{16}' "sentinel validation"
            Assert-Text $validationText 'sentinelChecks=[0-9A-F]{8}'; Assert-Text $validationText 'objectBefore=[0-9A-F]{8}'; Assert-Text $validationText 'objectAfter=[0-9A-F]{8}'; Assert-Text $validationText 'objectAtStop=[0-9A-F]{8}'; Assert-Text $validationText 'objectHistoryOverflow=00000000' "object validation"
            Assert-Text $validationText 'context=[0-9A-F]{16}'; Assert-Text $validationText 'contextFieldReads=00000006'; Assert-Text $validationText 'contextThread=[0-9A-F]{16}'; Assert-Text $validationText 'contextStackLimit=[0-9A-F]{16}'; Assert-Text $validationText 'contextThreadNumber=[0-9A-F]{16}'; Assert-Text $validationText 'contextThreadCount=[0-9A-F]{16}'; Assert-Text $validationText 'contextPromotion=00000001'; Assert-Text $validationText 'contextConcurrent=00000000' "live ScanContext"
            Assert-Text $validationText 'lockHeld=00000001'; Assert-Text $validationText 'eeSuspended=00000001'; Assert-Text $validationText 'managedEntryProhibited=00000001' "suspension invariant"
            Assert-Text $validationText 'restartRequests=00000000'; Assert-Text $validationText 'restartEntries=00000000'; Assert-Text $validationText 'managedResume=00000000' "zero restart and resume"
            $callbackSiteSlot=Get-MarkerField $validationText 'callbackSiteSlot'; $callbackSiteRaw=Get-MarkerField $validationText 'callbackSiteRaw'; $callbackSiteContext=Get-MarkerField $validationText 'callbackSiteContext'; $callbackSiteCallback=Get-MarkerField $validationText 'callbackSiteCallback'; $callbackEntryAddress=Get-MarkerField $validationText 'callbackEntryAddress'
            $callbackArg1=Get-MarkerField $validationText 'arg1'; $callbackArg2=Get-MarkerField $validationText 'arg2'; $callbackArg3=Get-MarkerField $validationText 'arg3'; $callbackRawRcx=Get-MarkerField $validationText 'rawRcx'; $callbackRawRdx=Get-MarkerField $validationText 'rawRdx'; $callbackRawR8=Get-MarkerField $validationText 'rawR8'; $rootSlot=Get-MarkerField $validationText 'rootSlot'; $callbackLoadedRoot=Get-MarkerField $validationText 'callbackLoadedRoot'; $membershipObject=Get-MarkerField $validationText 'membershipObject'; $storageObject=Get-MarkerField $validationText 'storageObject'; $membershipLower=Get-MarkerField $validationText 'lowerBound'; $membershipUpper=Get-MarkerField $validationText 'upperBound'
            $objectValue=[Convert]::ToUInt64($membershipObject.Substring(2),16); $lowerValue=[Convert]::ToUInt64($membershipLower.Substring(2),16); $upperValue=[Convert]::ToUInt64($membershipUpper.Substring(2),16)
            if ($callbackArg1 -ne $callbackRawRcx -or $callbackArg2 -ne $callbackRawRdx -or $callbackArg3 -ne $callbackRawR8 -or $callbackArg1 -ne $callbackSiteSlot -or $callbackArg1 -ne $rootSlot) { throw "Membership callback ABI/root slot mismatch in $name." }
            if ($callbackLoadedRoot -ne $callbackSiteRaw -or $callbackLoadedRoot -ne $storageObject -or $membershipObject -ne $callbackLoadedRoot -or (Get-MarkerField $validationText 'objectMatchesCallbackRoot') -ne '0x00000001') { throw "Membership input did not equal callback-loaded storage object in $name." }
            if ($callbackArg2 -ne $callbackSiteContext -or $callbackSiteCallback -ne $callbackEntryAddress -or $lowerValue -gt $objectValue -or $objectValue -ge $upperValue) { throw "Membership callback/range identity mismatch in $name." }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker="C011EC08"; outcome="A"; harnessTerminated=$true
                callbackRequestCount=(Get-MarkerField $validationText 'callbackRequestCount'); callbackCallSiteCount=(Get-MarkerField $validationText 'callbackCallSiteCount'); callbackInvocationCount=(Get-MarkerField $validationText 'callbackInvocationCount'); callbackEntryCount=(Get-MarkerField $validationText 'callbackEntryCount'); callbackReturnCount=(Get-MarkerField $validationText 'callbackReturnCount'); duplicateCallbackInvocations=(Get-MarkerField $validationText 'secondCallbackAttempts')
                callbackSite=[ordered]@{slot=$callbackSiteSlot;rawValue=$callbackSiteRaw;scanContext=$callbackSiteContext;callback=$callbackSiteCallback}
                callbackEntry=[ordered]@{address=$callbackEntryAddress;returnAddress=(Get-MarkerField $validationText 'callbackEntryReturn');rawRcx=$callbackRawRcx;rawRdx=$callbackRawRdx;rawR8=$callbackRawR8;arg1=$callbackArg1;arg2=$callbackArg2;arg3=$callbackArg3}
                root=[ordered]@{slot=$rootSlot;rawValue=$callbackLoadedRoot;loadedValue=$callbackLoadedRoot;storageObject=$storageObject;sentinel=(Get-MarkerField $validationText 'sentinelAddress')}
                scanContext=[ordered]@{address=(Get-MarkerField $validationText 'callbackContext');threadUnderCrawl=(Get-MarkerField $validationText 'contextThread');stackLimit=(Get-MarkerField $validationText 'contextStackLimit');threadNumber=(Get-MarkerField $validationText 'contextThreadNumber');threadCount=(Get-MarkerField $validationText 'contextThreadCount');promotion=(Get-MarkerField $validationText 'contextPromotion');concurrent=(Get-MarkerField $validationText 'contextConcurrent')}
                membership=[ordered]@{requests=(Get-MarkerField $validationText 'membershipRequests');entries=(Get-MarkerField $validationText 'membershipEntries');completions=(Get-MarkerField $validationText 'membershipCompletions');returns=(Get-MarkerField $validationText 'membershipReturns');duplicates=(Get-MarkerField $validationText 'duplicateChecks');object=$membershipObject;lowerBound=$membershipLower;upperBound=$membershipUpper;lowerEvaluated=(Get-MarkerField $validationText 'lowerEvaluated');upperEvaluated=(Get-MarkerField $validationText 'upperEvaluated');lowerResult=(Get-MarkerField $validationText 'lowerResult');upperResult=(Get-MarkerField $validationText 'upperResult');result=(Get-MarkerField $validationText 'inFindObjectRange');sourceBranch=(Get-MarkerField $validationText 'sourceBranch');completionReturnAddress=(Get-MarkerField $validationText 'membershipCompletionReturnAddress');postCheckReturnAddress=(Get-MarkerField $validationText 'membershipPostCheckReturnAddress')}
                prohibited=[ordered]@{generationClassificationStart=(Get-MarkerField $validationText 'generationClassificationStart');generationQueryStart=(Get-MarkerField $validationText 'generationQueryStart');condemnedGenerationComparisons=(Get-MarkerField $validationText 'condemnedGenerationComparisons');postMembershipSegmentLookup=(Get-MarkerField $validationText 'postMembershipSegmentLookup');objectDereferences=(Get-MarkerField $validationText 'objectDereferences');objectHeaders=(Get-MarkerField $validationText 'objectHeaders');methodTables=(Get-MarkerField $validationText 'methodTables');promotionStart=(Get-MarkerField $validationText 'promotionStart');promotions=(Get-MarkerField $validationText 'promotions');markingStart=(Get-MarkerField $validationText 'markingStart');graphTraversal=(Get-MarkerField $validationText 'graphTraversal')}
                mutation=[ordered]@{markWrites=(Get-MarkerField $validationText 'markWrites');promotionWrites=(Get-MarkerField $validationText 'promotionWrites');objectMemory=(Get-MarkerField $validationText 'objectMutation');gcMetadata=(Get-MarkerField $validationText 'gcMetadataMutation');segmentMetadata=(Get-MarkerField $validationText 'segmentMetadataMutation')}
                managedProofRoot=[ordered]@{assignmentCount=(Get-MarkerField $validationText 'managedAssignmentCount');clearCount=(Get-MarkerField $validationText 'managedClearCount');readbackCount=(Get-MarkerField $validationText 'managedReadbackCount');assignmentValid=(Get-MarkerField $validationText 'managedAssignmentValid');readbackValid=(Get-MarkerField $validationText 'managedReadbackValid');sentinelAddress=(Get-MarkerField $validationText 'sentinelAddress');sentinelReadback=(Get-MarkerField $validationText 'sentinelReadback');storageObject=$storageObject;objectBefore=(Get-MarkerField $validationText 'objectBefore');objectAfter=(Get-MarkerField $validationText 'objectAfter');objectAtStop=(Get-MarkerField $validationText 'objectAtStop');sentinelChecks=(Get-MarkerField $validationText 'sentinelChecks');objectHistoryOverflow=(Get-MarkerField $validationText 'objectHistoryOverflow')}
                threadStore=[ordered]@{managedThread=(Get-MarkerField $validationText 'managedThread');currentThread=(Get-MarkerField $validationText 'currentThread');lockOwner=(Get-MarkerField $validationText 'lockOwner');lockHeld=(Get-MarkerField $validationText 'lockHeld');eeSuspended=(Get-MarkerField $validationText 'eeSuspended');managedEntryProhibited=(Get-MarkerField $validationText 'managedEntryProhibited')}
                restartRequests=(Get-MarkerField $validationText 'restartRequests');restartEntries=(Get-MarkerField $validationText 'restartEntries');managedResume=(Get-MarkerField $validationText 'managedResume')
            }
        } elseif ($isFirstRootCallbackEntry) {
            Assert-Text $validationText '\[nativeaot-gc-first-root-callback-entry\] SAFE_STOP marker=C011EC07' "first real callback-entry safe-stop marker"
            Assert-Text $validationText 'requestCount=00000001 callSiteCount=00000001 invocationCount=00000001 entryCount=00000001 returnCount=00000000 duplicateInvocations=00000000' "exactly one callback request, call-site entry, invocation, and callback entry"
            Assert-Text $validationText 'callbackRootLoads=00000001 callbackLoadedRaw=[0-9A-F]{16} nullTests=00000000 nullNonNull=00000000' "one locked callback-side root load and no null test"
            Assert-Text $validationText 'contextFieldReads=00000006 context=[0-9A-F]{16} contextThread=[0-9A-F]{16} contextStackLimit=[0-9A-F]{16} contextThreadNumber=[0-9A-F]{16} contextThreadCount=[0-9A-F]{16} contextPromotion=00000001 contextConcurrent=00000000' "ScanContext fields read without mutation"
            Assert-Text $validationText 'entryArgsMatch=00000001 slotMatch=00000001 rawMatchesStorage=00000001 contextMatch=00000001 flagsMatch=00000001' "live callback ABI argument validation"
            Assert-Text $validationText 'candidateClassification=00000000 heapMembership=00000000 segmentLookup=00000000 objectHeaders=00000000 methodTables=00000000' "stop before candidate classification and metadata access"
            Assert-Text $validationText 'promotionStart=00000000 promotions=00000000 markingStart=00000000 graphTraversal=00000000 markWrites=00000000 promotionWrites=00000000 objectMutation=00000000 gcMetadataMutation=00000000 segmentMetadataMutation=00000000' "zero promotion, marking, graph traversal, and mutation"
            Assert-Text $validationText 'managedAssignmentCount=00000001 managedClearCount=00000000 managedReadbackCount=00000001 managedAssignmentValid=00000001 managedReadbackValid=00000001' "managed ThreadStatic assignment and readback"
            Assert-Text $validationText 'threadStaticInitialization=0000000[23] sentinelOrdinal=00000000 sentinelAddress=[0-9A-F]{16} sentinelSize=0000000000001000 readbackAddress=[0-9A-F]{16} readbackExactMatch=00000001' "selected sentinel validation"
            Assert-Text $validationText 'firstNonNullSlot=[0-9A-F]{16} firstNonNullValue=[0-9A-F]{16} expectedStorageObject=[0-9A-F]{16} expectedSentinel=[0-9A-F]{16}' "real storage root identity"
            Assert-Text $validationText 'storageObject=[0-9A-F]{16} inlinedRoot=[0-9A-F]{16} objectBefore=00000025 objectAfter=00000025 objectAtStop=00000025 sentinelChecks=00000094 objectHistoryOverflow=00000000' "object and runtime ThreadStatic storage validation"
            Assert-Text $validationText 'lockHeld=00000001 eeSuspended=00000001 managedEntryProhibited=00000001' "thread-store lock and suspended EE invariant"
            Assert-Text $validationText 'restartRequests=00000000 restartEntries=00000000 managedResume=00000000 fixupFailures=00000000 rootFailures=00000000' "zero restart, resume, fixup, and root failures"
            $callbackSiteSlot = Get-MarkerField $validationText 'callbackSiteSlot'
            $callbackSiteRaw = Get-MarkerField $validationText 'callbackSiteRaw'
            $callbackSiteContext = Get-MarkerField $validationText 'callbackSiteContext'
            $callbackSiteCallback = Get-MarkerField $validationText 'callbackSiteCallback'
            $callbackEntryAddress = Get-MarkerField $validationText 'callbackEntryAddress'
            $callbackArg1 = Get-MarkerField $validationText 'arg1'
            $callbackArg2 = Get-MarkerField $validationText 'arg2'
            $callbackArg3 = Get-MarkerField $validationText 'arg3'
            $callbackRawRcx = Get-MarkerField $validationText 'rawRcx'
            $callbackRawRdx = Get-MarkerField $validationText 'rawRdx'
            $callbackRawR8 = Get-MarkerField $validationText 'rawR8'
            $firstNonNullSlot = Get-MarkerField $validationText 'firstNonNullSlot'
            $firstNonNullValue = Get-MarkerField $validationText 'firstNonNullValue'
            $expectedStorage = Get-MarkerField $validationText 'expectedStorageObject'
            $callbackLoadedRaw = Get-MarkerField $validationText 'callbackLoadedRaw'
            $context = Get-MarkerField $validationText 'context'
            $expectedFlags = Get-MarkerField $validationText 'expectedFlags'
            $actualFlags = Get-MarkerField $validationText 'actualFlags'
            $managedThread = Get-MarkerField $validationText 'managedThread'
            $currentThread = Get-MarkerField $validationText 'currentThread'
            $lockOwner = Get-MarkerField $validationText 'lockOwner'
            $callbackArg3Value = [Convert]::ToUInt64($callbackArg3.Substring(2), 16)
            $expectedFlagsValue = [Convert]::ToUInt64($expectedFlags.Substring(2), 16)
            $actualFlagsValue = [Convert]::ToUInt64($actualFlags.Substring(2), 16)
            if ($callbackArg1 -ne $callbackRawRcx -or $callbackArg2 -ne $callbackRawRdx -or $callbackArg3 -ne $callbackRawR8) { throw "Raw AMD64 callback registers did not normalize identically in $name." }
            if ($callbackArg1 -ne $callbackSiteSlot -or $callbackArg1 -ne $firstNonNullSlot) { throw "Callback argument 1 did not equal the real inline root slot in $name." }
            if ($callbackLoadedRaw -ne $callbackSiteRaw -or $callbackLoadedRaw -ne $firstNonNullValue -or $callbackLoadedRaw -ne $expectedStorage) { throw "Callback-side root load did not equal the real NativeAOT storage object in $name." }
            if ($callbackArg2 -ne $callbackSiteContext -or $callbackArg2 -ne $context) { throw "Callback argument 2 did not equal the live ScanContext in $name." }
            if ($callbackArg3Value -ne $expectedFlagsValue -or $callbackArg3Value -ne $actualFlagsValue) { throw "Callback argument 3 did not equal the expected root metadata flags in $name." }
            if ($callbackSiteCallback -ne $callbackEntryAddress) { throw "Callback call-site identity did not equal the actual GCHeap::Promote entry address in $name." }
            if ($managedThread -ne $currentThread -or $managedThread -ne $lockOwner) { throw "Managed/current/lock-owner thread identities diverged at callback entry in $name." }
            if ($validationText -match 'ALL_PASS|ALL_FAIL|GC\.Collect|RhShutdown|GC_Shutdown') { throw "Unexpected completion or shutdown marker appeared in $name." }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker="C011EC07"; outcome="A"; harnessTerminated=$true
                callbackRequestCount=(Get-MarkerField $validationText 'requestCount'); callbackCallSiteCount=(Get-MarkerField $validationText 'callSiteCount'); callbackInvocationCount=(Get-MarkerField $validationText 'invocationCount'); callbackEntryCount=(Get-MarkerField $validationText 'entryCount'); callbackReturnCount=(Get-MarkerField $validationText 'returnCount'); duplicateCallbackInvocations=(Get-MarkerField $validationText 'duplicateInvocations')
                callbackSite=[ordered]@{ slot=$callbackSiteSlot; rawValue=$callbackSiteRaw; scanContext=$callbackSiteContext; callback=$callbackSiteCallback; returnAddress=(Get-MarkerField $validationText 'callbackSiteReturn') }
                callbackEntry=[ordered]@{ address=$callbackEntryAddress; returnAddress=(Get-MarkerField $validationText 'callbackEntryReturn'); stackPointer=(Get-MarkerField $validationText 'callbackEntryRsp'); rawRcx=$callbackRawRcx; rawRdx=$callbackRawRdx; rawR8=$callbackRawR8; arg1=$callbackArg1; arg2=$callbackArg2; arg3=$callbackArg3 }
                root=[ordered]@{ slot=(Get-MarkerField $validationText 'rootSlot'); rawValue=(Get-MarkerField $validationText 'rootRaw'); callbackSideLoadCount=(Get-MarkerField $validationText 'callbackRootLoads'); loadedValue=$callbackLoadedRaw; firstNonNullSlot=$firstNonNullSlot; firstNonNullValue=$firstNonNullValue; expectedStorageObject=$expectedStorage; expectedSentinel=(Get-MarkerField $validationText 'expectedSentinel') }
                scanContext=[ordered]@{ address=$context; threadUnderCrawl=(Get-MarkerField $validationText 'contextThread'); stackLimit=(Get-MarkerField $validationText 'contextStackLimit'); threadNumber=(Get-MarkerField $validationText 'contextThreadNumber'); threadCount=(Get-MarkerField $validationText 'contextThreadCount'); promotion=(Get-MarkerField $validationText 'contextPromotion'); concurrent=(Get-MarkerField $validationText 'contextConcurrent'); fieldReads=(Get-MarkerField $validationText 'contextFieldReads') }
                flags=[ordered]@{ expected=$expectedFlags; actual=$actualFlags; match=(Get-MarkerField $validationText 'flagsMatch') }
                semanticBoundary=[ordered]@{ firstOperation=(Get-MarkerField $validationText 'firstSemanticOperation'); candidateClassification=(Get-MarkerField $validationText 'candidateClassification'); heapMembership=(Get-MarkerField $validationText 'heapMembership'); segmentLookup=(Get-MarkerField $validationText 'segmentLookup'); objectHeaders=(Get-MarkerField $validationText 'objectHeaders'); methodTables=(Get-MarkerField $validationText 'methodTables'); promotionStart=(Get-MarkerField $validationText 'promotionStart'); promotions=(Get-MarkerField $validationText 'promotions'); markingStart=(Get-MarkerField $validationText 'markingStart'); graphTraversal=(Get-MarkerField $validationText 'graphTraversal') }
                mutation=[ordered]@{ markWrites=(Get-MarkerField $validationText 'markWrites'); promotionWrites=(Get-MarkerField $validationText 'promotionWrites'); objectMemory=(Get-MarkerField $validationText 'objectMutation'); gcMetadata=(Get-MarkerField $validationText 'gcMetadataMutation'); segmentMetadata=(Get-MarkerField $validationText 'segmentMetadataMutation') }
                managedProofRoot=[ordered]@{ assignmentCount=(Get-MarkerField $validationText 'managedAssignmentCount'); clearCount=(Get-MarkerField $validationText 'managedClearCount'); readbackCount=(Get-MarkerField $validationText 'managedReadbackCount'); assignmentValid=(Get-MarkerField $validationText 'managedAssignmentValid'); readbackValid=(Get-MarkerField $validationText 'managedReadbackValid'); sentinelOrdinal=(Get-MarkerField $validationText 'sentinelOrdinal'); sentinelAddress=(Get-MarkerField $validationText 'sentinelAddress'); sentinelSize=(Get-MarkerField $validationText 'sentinelSize'); readbackAddress=(Get-MarkerField $validationText 'readbackAddress'); readbackExactMatch=(Get-MarkerField $validationText 'readbackExactMatch'); storageObject=(Get-MarkerField $validationText 'storageObject'); inlinedRoot=(Get-MarkerField $validationText 'inlinedRoot'); objectBefore=(Get-MarkerField $validationText 'objectBefore'); objectAfter=(Get-MarkerField $validationText 'objectAfter'); objectAtStop=(Get-MarkerField $validationText 'objectAtStop'); sentinelChecks=(Get-MarkerField $validationText 'sentinelChecks'); objectHistoryOverflow=(Get-MarkerField $validationText 'objectHistoryOverflow') }
                threadStore=[ordered]@{ managedThread=$managedThread; currentThread=$currentThread; lockOwner=$lockOwner; lockHeld=(Get-MarkerField $validationText 'lockHeld'); eeSuspended=(Get-MarkerField $validationText 'eeSuspended'); managedEntryProhibited=(Get-MarkerField $validationText 'managedEntryProhibited') }
                restartRequests=(Get-MarkerField $validationText 'restartRequests'); restartEntries=(Get-MarkerField $validationText 'restartEntries'); managedResume=(Get-MarkerField $validationText 'managedResume')
            }
        } elseif ($isFirstNonNullRoot) {
            Assert-Text $validationText '\[nativeaot-gc-first-non-null-root-callback-boundary\] SAFE_STOP marker=C011EC06' "first non-null root callback-boundary safe-stop marker"
            Assert-Text $validationText 'gcScanRootsRequest=00000001 gcScanRootsEntry=00000001 foreachRequest=00000001 foreachEntry=00000001 iteratorInit=00000001' "real root dispatcher and iterator entry"
            Assert-Text $validationText 'registeredBefore=00000001 registeredAfter=0000000[01] enumerated=00000001 included=00000001 excluded=00000000' "actual registered thread enumeration and inclusion"
            Assert-Text $validationText 'listIntegrityFailures=00000000 duplicates=00000000 registryMutationBefore=00000000 registryMutationAfter=00000000' "thread-list integrity and closed-world mutation state"
            Assert-Text $validationText 'providerSource=thread-static-provider providerRuntime=thread-static-provider providerFunction=0000000[12]' "runtime-selected thread-static provider"
            Assert-Text $validationText 'managedAssignmentCount=00000001 managedClearCount=00000000 managedReadbackCount=00000001 managedAssignmentValid=00000001 managedReadbackValid=00000001' "managed thread-static assignment and readback"
            Assert-Text $validationText 'threadStaticInitialization=0000000[23] sentinelOrdinal=00000000 sentinelAddress=[0-9A-F]{16} sentinelSize=0000000000001000 readbackAddress=[0-9A-F]{16} readbackExactMatch=00000001' "thread-static initialization and selected sentinel"
            Assert-Text $validationText 'candidateVisited=0000000[1-8] nullCandidates=0000000[01] nonNullCandidates=00000001' "bounded candidate sequence and first non-null value"
            Assert-Text $validationText 'firstNonNullSlot=[0-9A-F]{16} firstNonNullValue=[0-9A-F]{16} firstNonNullKnownAddressMatch=[0-9A-F]{8} expectedSentinelAddress=[0-9A-F]{16} expectedStorageObjectAddress=[0-9A-F]{16}' "first non-null candidate raw value"
            Assert-Text $validationText 'loadRequests=0000000[1-8] loadEntries=0000000[1-8] machineWordLoads=0000000[1-8] duplicateLoads=00000000 loadFaults=00000000' "exactly-once bounded candidate loads"
            Assert-Text $validationText 'rootFlags=00000000 rootKind=00000001 callbacks=00000000 promotions=00000000 marking=00000000' "root metadata before callback"
            Assert-Text $validationText 'candidateDereferences=00000000 heapMembershipTests=00000000 objectHeaders=00000000 methodTables=00000000 rootFlagApplications=00000000' "no semantic candidate processing"
            Assert-Text $validationText 'objectMutation=00000000 restartRequests=00000000 restartEntries=00000000 managedResume=00000000' "no mutation, restart, or managed resume"
            Assert-Text $validationText 'objectBeforeLoad=00000025 objectAfterLoad=00000025 objectAtStop=00000025 sentinelChecks=00000094 objectHistoryOverflow=00000000' "object validation around non-null load"
            Assert-Text $validationText 'runtimeThreadStaticStorageAllocations=00000001 runtimeThreadStaticStoragePublications=00000001 runtimeThreadStaticStorageObject=[0-9A-F]{16} runtimeThreadStaticInlinedRoot=[0-9A-F]{16} totalAllocationRequestsObserved=[0-9A-F]{8}' "real thread-static storage allocation and publication"
            Assert-Text $validationText 'condemnedGeneration=[0-9A-F]{8} maxGeneration=[0-9A-F]{8} scanContextPromotion=[0-9A-F]{8} scanContextConcurrent=[0-9A-F]{8} scanContextIdentity=[0-9A-F]{16}' "GC callback scan-context metadata"
            Assert-Text $validationText 'candidateMatchesProofRoot=0000000[01] candidateMatchesStorageObject=0000000[01]' "known-address candidate classification"
            Assert-Text $validationText 'providerRequests=0000000[12] providerEntries=0000000[12] providerSkips=0000000[01]' "provider request, entry, and null-inline-root state"
            Assert-Text $validationText 'fixupFailures=00000000 rootFailures=00000000 eeSuspended=00000001 lockDepth=00000001 marker=C011EC06' "fixup, suspension, and lock invariants"
            $currentIdentity = Get-MarkerField $validationText 'current'
            $enumeratedIdentity = Get-MarkerField $validationText 'enumeratedThread'
            $initiatorIdentity = Get-MarkerField $validationText 'initiator'
            $lockOwnerIdentity = Get-MarkerField $validationText 'lockOwner'
            $candidateSlotIdentity = Get-MarkerField $validationText 'firstNonNullSlot'
            $candidateValueIdentity = Get-MarkerField $validationText 'firstNonNullValue'
            $sentinelIdentity = Get-MarkerField $validationText 'sentinelAddress'
            $readbackIdentity = Get-MarkerField $validationText 'readbackAddress'
            $expectedSentinelIdentity = Get-MarkerField $validationText 'expectedSentinelAddress'
            if ([string]::IsNullOrWhiteSpace($currentIdentity) -or $currentIdentity -ne $enumeratedIdentity -or $currentIdentity -ne $initiatorIdentity -or $currentIdentity -ne $lockOwnerIdentity) { throw "Thread identity mismatch in $name." }
            if ([string]::IsNullOrWhiteSpace($sentinelIdentity) -or $sentinelIdentity -ne $readbackIdentity -or $sentinelIdentity -ne $expectedSentinelIdentity) { throw "Managed thread-static readback did not match the selected sentinel in $name." }
            if ([string]::IsNullOrWhiteSpace($candidateSlotIdentity) -or [string]::IsNullOrWhiteSpace($candidateValueIdentity) -or $candidateValueIdentity -eq '0x0000000000000000') { throw "Non-null candidate identity was not captured in $name." }
            $candidateLineMatch = [regex]::Match($validationText, 'CANDIDATE ordinal=(?<ordinal>[0-9A-F]+) slot=(?<slot>[0-9A-F]{16}) rawValue=(?<raw>[0-9A-F]{16}) loadCount=(?<loads>[0-9A-F]+) duplicateLoads=(?<duplicates>[0-9A-F]+) null=(?<null>[0-9A-F]+) knownAddressMatch=(?<known>[0-9A-F]+) exactSelectedSentinelMatch=(?<exact>[0-9A-F]+) callback=(?<callback>[0-9A-F]{16}) scanContext=(?<scanContext>[0-9A-F]{16})')
            if (-not $candidateLineMatch.Success) { throw "Candidate record was not captured in $name." }
            if ($candidateLineMatch.Groups['slot'].Value -ne $candidateSlotIdentity.Substring(2) -or $candidateLineMatch.Groups['raw'].Value -ne $candidateValueIdentity.Substring(2)) { throw "Candidate record does not match the safe-stop candidate in $name." }
            if ($validationText -match 'ALL_PASS|ALL_FAIL|GC\.Collect|RhShutdown|GC_Shutdown') { throw "Unexpected completion or shutdown marker appeared in $name." }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker="C011EC06"; harnessTerminated=$true
                allocations=(Get-MarkerField $validationText 'objectAtStop'); userAllocations=(Get-MarkerField $validationText 'userAllocations'); userAllocationRequests=(Get-MarkerField $validationText 'userAllocationRequests'); userFast=(Get-MarkerField $validationText 'userFast'); userRare=(Get-MarkerField $validationText 'userRare'); userRefills=(Get-MarkerField $validationText 'userRefills'); userSameSegmentCommits=(Get-MarkerField $validationText 'userSameSegmentCommits'); userSegmentTransitions=(Get-MarkerField $validationText 'userSegmentTransitions'); collectionAllocationOrdinal=(Get-MarkerField $validationText 'collectionAllocationOrdinal'); runtimeThreadStaticStorageAllocations=(Get-MarkerField $validationText 'runtimeThreadStaticStorageAllocations'); runtimeThreadStaticStoragePublications=(Get-MarkerField $validationText 'runtimeThreadStaticStoragePublications'); runtimeThreadStaticStorageObject=(Get-MarkerField $validationText 'runtimeThreadStaticStorageObject'); runtimeThreadStaticInlinedRoot=(Get-MarkerField $validationText 'runtimeThreadStaticInlinedRoot'); totalAllocationRequestsObserved=(Get-MarkerField $validationText 'totalAllocationRequestsObserved'); gcScanRootsRequests=(Get-MarkerField $validationText 'gcScanRootsRequest'); gcScanRootsEntries=(Get-MarkerField $validationText 'gcScanRootsEntry'); foreachThreadEntries=(Get-MarkerField $validationText 'foreachEntry'); iteratorInitializations=(Get-MarkerField $validationText 'iteratorInit'); registeredThreads=(Get-MarkerField $validationText 'registeredBefore'); registeredAfterThreads=(Get-MarkerField $validationText 'registeredAfter'); enumeratedThreads=(Get-MarkerField $validationText 'enumerated'); includedThreads=(Get-MarkerField $validationText 'included'); excludedThreads=(Get-MarkerField $validationText 'excluded'); registryMutationBefore=(Get-MarkerField $validationText 'registryMutationBefore'); registryMutationAfter=(Get-MarkerField $validationText 'registryMutationAfter'); listIntegrityFailures=(Get-MarkerField $validationText 'listIntegrityFailures'); listDuplicates=(Get-MarkerField $validationText 'duplicates')
                threadRecord=[ordered]@{ ordinal=1; nativeThread=$enumeratedIdentity; nativeThreadId=(Get-MarkerField $validationText 'nativeId'); current=$currentIdentity; initiator=$initiatorIdentity; lockOwner=$lockOwnerIdentity; lifecycle=(Get-MarkerField $validationText 'lifecycle'); stateFlags=(Get-MarkerField $validationText 'stateFlags'); cooperative=(Get-MarkerField $validationText 'cooperative'); preemptive=(Get-MarkerField $validationText 'preemptive') }
                managedProofRoot=[ordered]@{ assignmentCount=(Get-MarkerField $validationText 'managedAssignmentCount'); clearCount=(Get-MarkerField $validationText 'managedClearCount'); readbackCount=(Get-MarkerField $validationText 'managedReadbackCount'); assignmentValid=(Get-MarkerField $validationText 'managedAssignmentValid'); readbackValid=(Get-MarkerField $validationText 'managedReadbackValid'); initialization=(Get-MarkerField $validationText 'threadStaticInitialization'); sentinelOrdinal=(Get-MarkerField $validationText 'sentinelOrdinal'); sentinelAddress=$sentinelIdentity; sentinelSize=(Get-MarkerField $validationText 'sentinelSize'); readbackAddress=$readbackIdentity; readbackExactMatch=(Get-MarkerField $validationText 'readbackExactMatch'); managedThread=(Get-MarkerField $validationText 'managedThread') }
                provider=[ordered]@{ source="thread-static-provider"; runtime="thread-static-provider"; function=(Get-MarkerField $validationText 'providerFunction'); requests=(Get-MarkerField $validationText 'providerRequests'); entries=(Get-MarkerField $validationText 'providerEntries'); skips=(Get-MarkerField $validationText 'providerSkips'); metadataContainer=(Get-MarkerField $validationText 'metadataContainer'); storageAddress=(Get-MarkerField $validationText 'storageAddress') }
                candidateSlots=@([ordered]@{ ordinal=("0x"+$candidateLineMatch.Groups['ordinal'].Value); slot=("0x"+$candidateLineMatch.Groups['slot'].Value); rawValue=("0x"+$candidateLineMatch.Groups['raw'].Value); loadCount=("0x"+$candidateLineMatch.Groups['loads'].Value); duplicateLoads=("0x"+$candidateLineMatch.Groups['duplicates'].Value); null=("0x"+$candidateLineMatch.Groups['null'].Value); knownAddressMatch=("0x"+$candidateLineMatch.Groups['known'].Value); exactSelectedSentinelMatch=("0x"+$candidateLineMatch.Groups['exact'].Value); callback=("0x"+$candidateLineMatch.Groups['callback'].Value); scanContext=("0x"+$candidateLineMatch.Groups['scanContext'].Value) })
                candidateVisited=(Get-MarkerField $validationText 'candidateVisited'); nullCandidates=(Get-MarkerField $validationText 'nullCandidates'); nonNullCandidates=(Get-MarkerField $validationText 'nonNullCandidates'); firstNonNullSlot=$candidateSlotIdentity; firstNonNullValue=$candidateValueIdentity; firstNonNullKnownAddressMatch=(Get-MarkerField $validationText 'firstNonNullKnownAddressMatch'); candidateMatchesProofRoot=(Get-MarkerField $validationText 'candidateMatchesProofRoot'); candidateMatchesStorageObject=(Get-MarkerField $validationText 'candidateMatchesStorageObject'); expectedStorageObjectAddress=(Get-MarkerField $validationText 'expectedStorageObjectAddress'); candidateProofRootObserved=(Get-MarkerField $validationText 'proofRootObserved'); loadRequests=(Get-MarkerField $validationText 'loadRequests'); loadEntries=(Get-MarkerField $validationText 'loadEntries'); machineWordLoads=(Get-MarkerField $validationText 'machineWordLoads'); duplicateLoads=(Get-MarkerField $validationText 'duplicateLoads'); loadFaults=(Get-MarkerField $validationText 'loadFaults'); callback=(Get-MarkerField $validationText 'callback'); scanContext=(Get-MarkerField $validationText 'scanContext'); rootFlags=(Get-MarkerField $validationText 'rootFlags'); rootKind=(Get-MarkerField $validationText 'rootKind'); condemnedGeneration=(Get-MarkerField $validationText 'condemnedGeneration'); maxGeneration=(Get-MarkerField $validationText 'maxGeneration'); scanContextPromotion=(Get-MarkerField $validationText 'scanContextPromotion'); scanContextConcurrent=(Get-MarkerField $validationText 'scanContextConcurrent'); scanContextIdentity=(Get-MarkerField $validationText 'scanContextIdentity')
                candidateDereferences=(Get-MarkerField $validationText 'candidateDereferences'); heapMembershipTests=(Get-MarkerField $validationText 'heapMembershipTests'); objectHeaders=(Get-MarkerField $validationText 'objectHeaders'); methodTables=(Get-MarkerField $validationText 'methodTables'); rootFlagApplications=(Get-MarkerField $validationText 'rootFlagApplications'); callbacks=(Get-MarkerField $validationText 'callbacks'); promotions=(Get-MarkerField $validationText 'promotions'); marking=(Get-MarkerField $validationText 'marking'); objectMutation=(Get-MarkerField $validationText 'objectMutation'); restartRequests=(Get-MarkerField $validationText 'restartRequests'); restartEntries=(Get-MarkerField $validationText 'restartEntries'); managedResume=(Get-MarkerField $validationText 'managedResume'); objectBeforeLoad=(Get-MarkerField $validationText 'objectBeforeLoad'); objectAfterLoad=(Get-MarkerField $validationText 'objectAfterLoad'); objectAtStop=(Get-MarkerField $validationText 'objectAtStop')
            }
        } elseif ($isFirstRootCandidateLoad) {
            # The shared NativeAOT module-initialization contract adds three
            # legitimate startup allocations and the general safe-stop marker
            # is emitted before the final frontier marker. Match the final
            # C011EC05 marker without weakening the candidate-load invariants.
            Assert-Text $validationText '\[nativeaot-gc-first-root-candidate-load\].*marker=C011EC05' "first root candidate-load safe-stop marker"
            Assert-Text $validationText 'gcScanRootsRequest=00000001 gcScanRootsEntry=00000001 foreachRequest=00000001 foreachEntry=00000001 iteratorInit=00000001' "real root dispatcher and iterator entry"
            Assert-Text $validationText 'registeredBefore=00000001 registeredAfter=00000001 enumerated=00000001 included=00000001 excluded=00000000' "actual registered thread enumeration and inclusion"
            Assert-Text $validationText 'providerSource=thread-static-provider providerRuntime=thread-static-provider providerFunction=00000002' "runtime-selected thread-static provider"
            Assert-Text $validationText 'providerRequests=00000002 providerEntries=00000001 providerSkips=00000001 metadataContainers=00000001' "provider request, entry, skip, and metadata counts"
            Assert-Text $validationText 'slotWidth=00000008 slotAlignment=00000000 slotMapped=00000001 slotCommitted=00000001 slotWritableContract=00000001 slotStable=00000001 slotExpectedThreadStorage=00000001' "real aligned mapped candidate slot"
            Assert-Text $validationText 'slotOverlapHeap=00000000 slotOverlapThread=00000001 slotOverlapAllocContext=00000000 slotOverlapStack=00000000 slotOverlapOther=00000000' "candidate slot region separation"
            Assert-Text $validationText 'loadRequests=00000001 loadEntries=00000001 machineWordLoads=00000001 duplicateLoads=00000000 loadFaults=00000000' "exactly one successful machine-word load"
            Assert-Text $validationText 'loadAddress=[0-9A-F]{16} rawValue=[0-9A-F]{16} valueIsNull=(?:00000000|00000001) knownAddressMatch=[0-9A-F]{8}' "saved opaque raw candidate value"
            Assert-Text $validationText 'candidateDereferences=00000000 heapMembershipTests=00000000 objectHeaders=00000000 methodTables=00000000 rootFlagApplications=00000000' "no semantic candidate processing"
            Assert-Text $validationText 'candidates=00000000 callbacks=00000000 promotions=00000000 marking=00000000 objectMutation=00000000 restartRequests=00000000 restartEntries=00000000 managedResume=00000000' "no callbacks, marking, mutation, restart, or resume"
            Assert-Text $validationText 'objectBeforeLoad=0000002[58] objectAfterLoad=0000002[58] objectAtStop=0000002[58] sentinelChecks=000000(?:94|A0)' "object and sentinel validation before and after load"
            Assert-Text $validationText 'fixupFailures=00000000 rootFailures=0000000[01] eeSuspended=00000001 lockDepth=00000001' "fixup, suspension, and lock invariants"
            $currentIdentity = Get-MarkerField $validationText 'current'
            $enumeratedIdentity = Get-MarkerField $validationText 'enumeratedThread'
            $initiatorIdentity = Get-MarkerField $validationText 'initiator'
            $lockOwnerIdentity = Get-MarkerField $validationText 'lockOwner'
            $candidateSlotIdentity = Get-MarkerField $validationText 'candidateSlot'
            $loadAddressIdentity = Get-MarkerField $validationText 'loadAddress'
            if ([string]::IsNullOrWhiteSpace($currentIdentity) -or $currentIdentity -ne $enumeratedIdentity -or $currentIdentity -ne $initiatorIdentity -or $currentIdentity -ne $lockOwnerIdentity) { throw "Thread identity mismatch in $name." }
            if ([string]::IsNullOrWhiteSpace($candidateSlotIdentity) -or $candidateSlotIdentity -ne $loadAddressIdentity) { throw "Candidate slot/load-address mismatch in $name." }
            if ($validationText -match 'ALL_PASS|ALL_FAIL|GC\.Collect|RhShutdown|GC_Shutdown') { throw "Unexpected completion or shutdown marker appeared in $name." }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker="C011EC05"; harnessTerminated=$true
                allocations=(Get-MarkerField $validationText 'objectAtStop'); gcScanRootsRequests=(Get-MarkerField $validationText 'gcScanRootsRequest'); gcScanRootsEntries=(Get-MarkerField $validationText 'gcScanRootsEntry'); foreachThreadEntries=(Get-MarkerField $validationText 'foreachEntry'); iteratorInitializations=(Get-MarkerField $validationText 'iteratorInit'); registeredThreads=(Get-MarkerField $validationText 'registeredBefore'); enumeratedThreads=(Get-MarkerField $validationText 'enumerated'); includedThreads=(Get-MarkerField $validationText 'included'); excludedThreads=(Get-MarkerField $validationText 'excluded')
                threadRecord=[ordered]@{ ordinal=1; nativeThread=$enumeratedIdentity; nativeThreadId=(Get-MarkerField $validationText 'nativeId'); current=$currentIdentity; initiator=$initiatorIdentity; lockOwner=$lockOwnerIdentity; lifecycle=(Get-MarkerField $validationText 'lifecycle'); stateFlags=(Get-MarkerField $validationText 'stateFlags'); cooperative=(Get-MarkerField $validationText 'cooperative'); preemptive=(Get-MarkerField $validationText 'preemptive'); allocationContext=(Get-MarkerField $validationText 'allocContext'); stackLow=(Get-MarkerField $validationText 'stackLow'); stackHigh=(Get-MarkerField $validationText 'stackHigh') }
                provider=[ordered]@{ source="thread-static-provider"; runtime="thread-static-provider"; function="Thread::GetThreadStaticStorage"; requests=(Get-MarkerField $validationText 'providerRequests'); entries=(Get-MarkerField $validationText 'providerEntries'); skips=(Get-MarkerField $validationText 'providerSkips'); metadataContainers=(Get-MarkerField $validationText 'metadataContainers'); metadataContainer=(Get-MarkerField $validationText 'metadataContainer') }
                candidateSlot=$candidateSlotIdentity; slotOffset=(Get-MarkerField $validationText 'slotOffset'); slotWidth=(Get-MarkerField $validationText 'slotWidth'); slotAlignment=(Get-MarkerField $validationText 'slotAlignment'); slotMapped=(Get-MarkerField $validationText 'slotMapped'); slotCommitted=(Get-MarkerField $validationText 'slotCommitted'); slotWritableContract=(Get-MarkerField $validationText 'slotWritableContract'); slotStable=(Get-MarkerField $validationText 'slotStable'); slotExpectedThreadStorage=(Get-MarkerField $validationText 'slotExpectedThreadStorage')
                rootFlags=(Get-MarkerField $validationText 'rootFlags'); rootKind=(Get-MarkerField $validationText 'rootKind'); callback=(Get-MarkerField $validationText 'callback'); scanContext=(Get-MarkerField $validationText 'scanContext'); loadRequests=(Get-MarkerField $validationText 'loadRequests'); loadEntries=(Get-MarkerField $validationText 'loadEntries'); machineWordLoads=(Get-MarkerField $validationText 'machineWordLoads'); duplicateLoads=(Get-MarkerField $validationText 'duplicateLoads'); loadFaults=(Get-MarkerField $validationText 'loadFaults'); loadAddress=$loadAddressIdentity; rawValue=(Get-MarkerField $validationText 'rawValue'); valueIsNull=(Get-MarkerField $validationText 'valueIsNull'); knownAddressMatch=(Get-MarkerField $validationText 'knownAddressMatch')
                candidateDereferences=(Get-MarkerField $validationText 'candidateDereferences'); heapMembershipTests=(Get-MarkerField $validationText 'heapMembershipTests'); objectHeaders=(Get-MarkerField $validationText 'objectHeaders'); methodTables=(Get-MarkerField $validationText 'methodTables'); rootFlagApplications=(Get-MarkerField $validationText 'rootFlagApplications'); candidates=(Get-MarkerField $validationText 'candidates'); callbacks=(Get-MarkerField $validationText 'callbacks'); promotions=(Get-MarkerField $validationText 'promotions'); marking=(Get-MarkerField $validationText 'marking'); objectMutation=(Get-MarkerField $validationText 'objectMutation'); restartRequests=(Get-MarkerField $validationText 'restartRequests'); restartEntries=(Get-MarkerField $validationText 'restartEntries'); managedResume=(Get-MarkerField $validationText 'managedResume'); objectBeforeLoad=(Get-MarkerField $validationText 'objectBeforeLoad'); objectAfterLoad=(Get-MarkerField $validationText 'objectAfterLoad'); objectAtStop=(Get-MarkerField $validationText 'objectAtStop'); sentinelChecks=(Get-MarkerField $validationText 'sentinelChecks')
            }
        } elseif ($isFirstPerThreadRootProvider) {
            Assert-Text $validationText '\[nativeaot-gc-first-per-thread-root-provider\] SAFE_STOP marker=C011EC04' "first per-thread provider safe-stop marker"
            Assert-Text $validationText 'gcScanRootsRequest=00000001 gcScanRootsEntry=00000001 foreachRequest=00000001 foreachEntry=00000001 iteratorInit=00000001' "real root dispatcher and iterator entry"
            Assert-Text $validationText 'registeredBefore=00000001 registeredAfter=00000001 enumerated=00000001 included=00000001 excluded=00000000' "actual registered thread enumeration and inclusion"
            Assert-Text $validationText 'listIntegrityFailures=00000000 duplicates=00000000 registryMutationBefore=00000000 registryMutationAfter=00000000' "thread-list integrity and closed-world mutation state"
            Assert-Text $validationText 'providerSource=thread-static-provider providerRuntime=thread-static-provider providerFunction=00000002' "runtime-selected thread-static provider"
            Assert-Text $validationText 'providerRequests=00000002 providerEntries=00000001 providerSkips=00000001 metadataContainers=00000001' "provider request, entry, skip, and metadata counts"
            Assert-Text $validationText 'candidateMetadata=00000001 candidateReads=00000000 candidates=00000000 callbacks=00000000 promotions=00000000 marking=00000000' "candidate boundary before value access"
            Assert-Text $validationText 'objectMutation=00000000 restartRequests=00000000 restartEntries=00000000 managedResume=00000000' "no mutation, restart, or managed resume"
            Assert-Text $validationText 'stackBoundsRequested=00000000 stackScanning=00000000 threadStaticRequested=00000001 threadStaticScanning=00000000' "provider boundary before stack or static candidate scanning"
            Assert-Text $validationText 'sentinelChecks=000000A0 objectBefore=00000028 objectAfter=00000028' "sentinel and object validation at provider boundary"
            Assert-Text $validationText 'fixupFailures=00000000 rootFailures=00000000 eeSuspended=00000001 lockDepth=00000001' "fixup, suspension, and lock invariants"
            $currentIdentity = Get-MarkerField $validationText 'current'
            $enumeratedIdentity = Get-MarkerField $validationText 'enumeratedThread'
            $initiatorIdentity = Get-MarkerField $validationText 'initiator'
            $lockOwnerIdentity = Get-MarkerField $validationText 'lockOwner'
            if ([string]::IsNullOrWhiteSpace($currentIdentity) -or $currentIdentity -ne $enumeratedIdentity -or $currentIdentity -ne $initiatorIdentity -or $currentIdentity -ne $lockOwnerIdentity) { throw "Thread identity mismatch in $name." }
            if ($validationText -match 'ALL_PASS|ALL_FAIL|GC\.Collect|RhShutdown|GC_Shutdown') { throw "Unexpected completion or shutdown marker appeared in $name." }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker="C011EC04"; harnessTerminated=$true
                allocations=(Get-MarkerField $validationText 'objectAfter'); gcScanRootsRequests=(Get-MarkerField $validationText 'gcScanRootsRequest'); gcScanRootsEntries=(Get-MarkerField $validationText 'gcScanRootsEntry'); foreachThreadEntries=(Get-MarkerField $validationText 'foreachEntry'); iteratorInitializations=(Get-MarkerField $validationText 'iteratorInit'); registeredThreads=(Get-MarkerField $validationText 'registeredBefore'); enumeratedThreads=(Get-MarkerField $validationText 'enumerated'); includedThreads=(Get-MarkerField $validationText 'included'); excludedThreads=(Get-MarkerField $validationText 'excluded')
                threadRecord=[ordered]@{ ordinal=1; nativeThread=$enumeratedIdentity; nativeThreadId=(Get-MarkerField $validationText 'nativeId'); current=$currentIdentity; initiator=$initiatorIdentity; lockOwner=$lockOwnerIdentity; lifecycle=(Get-MarkerField $validationText 'lifecycle'); stateFlags=(Get-MarkerField $validationText 'stateFlags'); cooperative=(Get-MarkerField $validationText 'cooperative'); preemptive=(Get-MarkerField $validationText 'preemptive'); allocationContext=(Get-MarkerField $validationText 'allocContext'); stackLow=(Get-MarkerField $validationText 'stackLow'); stackHigh=(Get-MarkerField $validationText 'stackHigh'); listHeadBefore=(Get-MarkerField $validationText 'listHeadBefore'); listTailBefore=(Get-MarkerField $validationText 'listTailBefore'); listHeadAfter=(Get-MarkerField $validationText 'listHeadAfter'); listTailAfter=(Get-MarkerField $validationText 'listTailAfter'); listIntegrityFailures=(Get-MarkerField $validationText 'listIntegrityFailures'); duplicates=(Get-MarkerField $validationText 'duplicates') }
                providerSource="thread-static-provider"; providerRuntime="thread-static-provider"; providerFunction="Thread::GetThreadStaticStorage"; providerRequests=(Get-MarkerField $validationText 'providerRequests'); providerEntries=(Get-MarkerField $validationText 'providerEntries'); providerSkips=(Get-MarkerField $validationText 'providerSkips'); metadataContainers=(Get-MarkerField $validationText 'metadataContainers'); firstMetadata=(Get-MarkerField $validationText 'firstMetadata'); candidateMetadata=(Get-MarkerField $validationText 'candidateMetadata'); candidateReads=(Get-MarkerField $validationText 'candidateReads'); candidates=(Get-MarkerField $validationText 'candidates'); callbacks=(Get-MarkerField $validationText 'callbacks'); promotions=(Get-MarkerField $validationText 'promotions'); marking=(Get-MarkerField $validationText 'marking'); objectMutation=(Get-MarkerField $validationText 'objectMutation'); restartRequests=(Get-MarkerField $validationText 'restartRequests'); restartEntries=(Get-MarkerField $validationText 'restartEntries'); managedResume=(Get-MarkerField $validationText 'managedResume'); stackBoundsRequested=(Get-MarkerField $validationText 'stackBoundsRequested'); stackScanning=(Get-MarkerField $validationText 'stackScanning'); threadStaticRequested=(Get-MarkerField $validationText 'threadStaticRequested'); threadStaticScanning=(Get-MarkerField $validationText 'threadStaticScanning'); sentinelChecks=(Get-MarkerField $validationText 'sentinelChecks'); objectBefore=(Get-MarkerField $validationText 'objectBefore'); objectAfter=(Get-MarkerField $validationText 'objectAfter')
            }
        } elseif ($isAllocationContextFixupRootBoundary) {
            Assert-Text $validationText '\[nativeaot-gc-allocation-context-fixup-root-boundary\] SAFE_STOP marker=C011EC03' "unique root-boundary safe-stop marker"
            Assert-Text $validationText 'callback=GCToEEInterface::GcScanRoots entry before FOREACH_THREAD' "root dispatcher entry before thread iteration"
            Assert-Text $validationText 'fixupRequest=00000001 fixupEntry=00000001 fixupComplete=00000001' "real fix_allocation_contexts request, enumeration, and completion"
            Assert-Text $validationText 'contextsVisited=00000001 contextsChanged=00000001 contextsCleared=00000001' "one real context changed and was cleared"
            $fixupObjectBefore = Get-MarkerField $validationText 'objectBefore'
            $fixupObjectAfter = Get-MarkerField $validationText 'objectAfter'
            if ([string]::IsNullOrWhiteSpace($fixupObjectBefore) -or $fixupObjectBefore -ne $fixupObjectAfter) { throw "Allocation-context fixup changed the bounded object history in $name." }
            Assert-Text $validationText 'rootDispatcher=00000001 rootProviders=00000000 rootCandidates=00000000 callbacks=00000000 marking=00000000' "root boundary before providers and marking"
            Assert-Text $validationText 'metadataMutation=00000001 objectMutation=00000000 restartResume=00000000' "metadata-only fixup mutation and no restart"
            Assert-Text $validationText 'fixupFailures=00000000 rootFailures=00000000 objectFailuresBefore=00000000 objectFailuresAfter=00000000' "zero fixup, root, and object-validation failures"
            Assert-Text $validationText 'boundaryFailures=00000000 patternFailures=00000000 addressChanges=00000000' "zero object boundary, pattern, and address-change failures"
            $fixupSentinelChecks = [Convert]::ToUInt32((Get-MarkerField $validationText 'sentinelChecks').Substring(2), 16)
            if ($fixupSentinelChecks -lt 4) { throw "Fewer than four live sentinels were checked before root dispatch in $name." }
            if ($validationText -match 'ALL_PASS|ALL_FAIL|GC\.Collect|RhShutdown|GC_Shutdown') { throw "Unexpected completion or shutdown marker appeared in $name." }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker="C011EC03"; harnessTerminated=$true
                 allocations=$fixupObjectAfter; contextFixupRequests=(Get-MarkerField $validationText 'fixupRequest'); contextFixupEntries=(Get-MarkerField $validationText 'fixupEntry'); contextFixupCompletions=(Get-MarkerField $validationText 'fixupComplete'); contextsVisited=(Get-MarkerField $validationText 'contextsVisited'); contextsChanged=(Get-MarkerField $validationText 'contextsChanged'); contextsCleared=(Get-MarkerField $validationText 'contextsCleared')
                 objectValidationBefore=$fixupObjectBefore; objectValidationAfter=$fixupObjectAfter; rootDispatcherEntries=(Get-MarkerField $validationText 'rootDispatcher'); rootProviderEntries=(Get-MarkerField $validationText 'rootProviders'); rootCandidates=(Get-MarkerField $validationText 'rootCandidates'); rootCallbacks=(Get-MarkerField $validationText 'callbacks'); markingEntries=(Get-MarkerField $validationText 'marking'); metadataMutation=(Get-MarkerField $validationText 'metadataMutation'); objectMutation=(Get-MarkerField $validationText 'objectMutation'); restartResume=(Get-MarkerField $validationText 'restartResume')
                fixupMode=(Get-MarkerField $validationText 'fixupMode'); enumerationComplete=(Get-MarkerField $validationText 'enumerationComplete'); activeBefore=(Get-MarkerField $validationText 'activeBefore'); activeAfter=(Get-MarkerField $validationText 'activeAfter'); retired=(Get-MarkerField $validationText 'retired'); metadataComplete=(Get-MarkerField $validationText 'metadataComplete'); segmentBookkeeping=(Get-MarkerField $validationText 'segmentBookkeeping'); fixupFailures=(Get-MarkerField $validationText 'fixupFailures'); rootFailures=(Get-MarkerField $validationText 'rootFailures'); objectFailuresBefore=(Get-MarkerField $validationText 'objectFailuresBefore'); objectFailuresAfter=(Get-MarkerField $validationText 'objectFailuresAfter'); boundaryFailures=(Get-MarkerField $validationText 'boundaryFailures'); patternFailures=(Get-MarkerField $validationText 'patternFailures'); addressChanges=(Get-MarkerField $validationText 'addressChanges'); sentinelChecks=(Get-MarkerField $validationText 'sentinelChecks')
                 allocationPointerBefore=(Get-MarkerField $validationText 'allocPtrBefore'); allocationLimitBefore=(Get-MarkerField $validationText 'allocLimitBefore'); allocationPointerAfter=(Get-MarkerField $validationText 'allocPtrAfter'); allocationLimitAfter=(Get-MarkerField $validationText 'allocLimitAfter'); validExtentBefore=(Get-MarkerField $validationText 'validExtentBefore'); validExtentAfter=(Get-MarkerField $validationText 'validExtentAfter'); unusedTailBefore=(Get-MarkerField $validationText 'unusedTailBefore'); unusedTailAfter=(Get-MarkerField $validationText 'unusedTailAfter'); heapCounterBefore=(Get-MarkerField $validationText 'heapCounterBefore'); heapCounterAfter=(Get-MarkerField $validationText 'heapCounterAfter'); segmentAllocatedBefore=(Get-MarkerField $validationText 'segmentAllocatedBefore'); segmentAllocatedAfter=(Get-MarkerField $validationText 'segmentAllocatedAfter'); segmentCommittedBefore=(Get-MarkerField $validationText 'segmentCommittedBefore'); segmentCommittedAfter=(Get-MarkerField $validationText 'segmentCommittedAfter'); segmentReservedBefore=(Get-MarkerField $validationText 'segmentReservedBefore'); segmentReservedAfter=(Get-MarkerField $validationText 'segmentReservedAfter')
            }
        } else {
            Assert-Text $validationText '\[nativeaot-gc-single-thread-suspend-ee\] SAFE_STOP marker=C011EC02' "unique safe-stop marker"
        Assert-Text $validationText 'callback=GCHeap::GarbageCollect before fix_allocation_contexts' "safe boundary after EE mode restoration"
        Assert-Text $validationText 'requestCount=00000001 entryCount=00000001' "collection request and entry"
        Assert-Text $validationText 'requestedGeneration=00000001 reason=00000005' "generation and reason"
        Assert-Text $validationText 'suspendReason=00000001' "SuspendEE reason"
        Assert-Text $validationText 'suspendEeEntryCount=00000001 suspendEeReturnCount=00000001 suspendEeSuspensionCount=00000001' "SuspendEE entry, suspension, and return"
        Assert-Text $validationText 'lockRequests=00000001 lockAcquisitions=00000001 lockFailures=00000000 unlocks=00000000' "real thread-store lock state"
        Assert-Text $validationText 'lockDepth=00000001 registeredThreads=00000001' "single registered mutator"
        Assert-Text $validationText 'identitiesMatch=00000001 expectedOtherMutators=00000000 stoppedOtherMutators=00000000' "initiator and peer state"
        Assert-Text $validationText 'currentThreadExempt=00000001 managedEntryProhibited=00000001 eeSuspended=00000001' "suspended EE state"
        Assert-Text $validationText 'nextBoundary=00000002 rootRequests=00000000 rootEntries=00000000 stackWalkRequests=00000000 stackWalkEntries=00000000 handleScanRequests=00000000 handleScanEntries=00000000' "pre-root boundary"
        Assert-Text $validationText 'heapMutationStarted=00000000 restartRequests=00000000 restartEntries=00000000 managedResumeCount=00000000' "no heap mutation or restart"
        Assert-Text $validationText 'registryMutationAttemptsWhileLocked=00000000 adapterRegistrations=00000001' "closed-world registry invariant and adapter"
        $suspendAllocations = [Convert]::ToUInt32((Get-MarkerField $validationText 'allocations').Substring(2), 16)
        $suspendFast = [Convert]::ToUInt32((Get-MarkerField $validationText 'fast').Substring(2), 16)
        $suspendRare = [Convert]::ToUInt32((Get-MarkerField $validationText 'rare').Substring(2), 16)
        $suspendRefills = [Convert]::ToUInt32((Get-MarkerField $validationText 'refills').Substring(2), 16)
        $suspendSameSegmentCommits = [Convert]::ToUInt32((Get-MarkerField $validationText 'sameSegmentCommits').Substring(2), 16)
        if ($suspendAllocations -eq 0 -or $suspendFast -eq 0 -or $suspendRare -eq 0 -or $suspendRefills -eq 0 -or $suspendSameSegmentCommits -eq 0 -or (Get-MarkerField $validationText 'segmentTransitions') -ne '0x00000000') { throw "Allocation/refill invariants were not preserved in $name." }
        Assert-Text $validationText 'sentinelFailures=00000000 liveSentinels=00000004' "live sentinel validation"
        if ($validationText -match 'ALL_PASS|ALL_FAIL|GC\.Collect|RhShutdown|GC_Shutdown') { throw "Unexpected completion or shutdown marker appeared in $name." }
        $runResults += [ordered]@{
            name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker="C011EC02"; harnessTerminated=$true
            allocations=(Get-MarkerField $validationText 'allocations'); fast=(Get-MarkerField $validationText 'fast'); rare=(Get-MarkerField $validationText 'rare')
            refills=(Get-MarkerField $validationText 'refills'); sameSegmentCommits=(Get-MarkerField $validationText 'sameSegmentCommits'); segmentTransitions=(Get-MarkerField $validationText 'segmentTransitions')
            collectionRequests=(Get-MarkerField $validationText 'requestCount'); collectionEntries=(Get-MarkerField $validationText 'entryCount'); suspendEeEntries=(Get-MarkerField $validationText 'suspendEeEntryCount'); suspendEeReturns=(Get-MarkerField $validationText 'suspendEeReturnCount')
            lockRequests=(Get-MarkerField $validationText 'lockRequests'); lockAcquisitions=(Get-MarkerField $validationText 'lockAcquisitions'); registeredThreads=(Get-MarkerField $validationText 'registeredThreads'); adapterRegistrations=(Get-MarkerField $validationText 'adapterRegistrations'); expectedOtherMutators=(Get-MarkerField $validationText 'expectedOtherMutators'); stoppedOtherMutators=(Get-MarkerField $validationText 'stoppedOtherMutators')
            rootEntries=(Get-MarkerField $validationText 'rootEntries'); heapMutationStarted=(Get-MarkerField $validationText 'heapMutationStarted'); restartEntries=(Get-MarkerField $validationText 'restartEntries')
        }
        }
    }

    if ($isC011EC30) {
        Invoke-LoggedCommand $objdump @('-d','-Mintel',$pePath) (Join-Path $runRoot 'artifact-disassembly.txt')
    }
    $identity = Get-Content -LiteralPath $identityManifestPath -Raw | ConvertFrom-Json
    $replacementHashes = @($identity.replacementObjects | ForEach-Object { [ordered]@{ path=$_.path; sha256=$_.sha256 } })
    if ($isFirstRootCondemnedGenerationDecision) {
        $nonC011EC10Runs = @($runResults | Where-Object { $_["safeStopMarker"] -ne "C011EC10" })
        if (@($runResults).Count -ne 3 -or $nonC011EC10Runs.Count -ne 0) { throw "The first real root condemned-generation experiment did not produce three C011EC10 runs." }
        $firstCondemnedRun = $runResults[0]
        $outcome = $firstCondemnedRun.outcome
        if (@($runResults | Where-Object { $_.outcome -ne $outcome }).Count -ne 0) { throw "Condemned-generation outcome was not deterministic across the three QEMU runs." }
        $condemnedSerial = Get-Content -LiteralPath $firstCondemnedRun.serial -Raw
        $manifest = [ordered]@{
            outcome="$outcome / first genuine NativeAOT root completed one real Workstation gc_heap::is_in_condemned_gc(o) decision; stopped before the selected true/false GCHeap::Promote continuation"
            proofMode=$ProofMode; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            taskStartCheckpoint=$taskStartCheckpoint
            productionStartup=[ordered]@{ initializeModules="production runtime path invoked before ManagedMain"; threadStatic="real NativeAOT [ThreadStatic] provider"; status="working" }
            ordinaryBaseline=[ordered]@{ startingBuildSha256=$ordinaryBuildBefore; startingEspSha256=$ordinaryEspBefore; expectedSha256=$normalKernelHash }
            runtimePack=[ordered]@{ version="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterface="5.3"; ee="2"; sourceCommit=$lockedCommit; lockedGcenvEeSourceSha256=(Hash-File $lockedEePath); lockedGcEnumSourceSha256=(Hash-File $lockedGcEnumPath); lockedGcCppSourceSha256=(Hash-File $lockedGcCppPath); lockedGcwksSourceSha256=(Hash-File $lockedGcwksPath); activePalArchiveSha256=(Hash-File $activeArchive); callbackSymbol=$callbackSymbolName; callbackAddress="0x$callbackAddressText"; generatedGcwks=$gcWksSource; generatedGcEnum=$gcEnumSource }
            prerequisite=[ordered]@{ marker="C011EC09"; report="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_HEAP_RESOLUTION.md"; historicalOutcome="B"; correctedInterpretation="heap_of(o)==0 is the valid Workstation single-heap sentinel when MULTIPLE_HEAPS is disabled" }
            managedProofRoot=[ordered]@{ field="[ThreadStatic] byte[]? s_gcProofThreadRoot"; sentinel=$firstCondemnedRun.root.sentinel; storageObject=$firstCondemnedRun.root.storageObject; inlinedRoot=(Get-MarkerField $condemnedSerial 'inlinedRoot'); assignmentCount=(Get-MarkerField $condemnedSerial 'managedAssignmentCount'); clearCount=(Get-MarkerField $condemnedSerial 'managedClearCount'); readbackCount=(Get-MarkerField $condemnedSerial 'managedReadbackCount'); assignmentValid=(Get-MarkerField $condemnedSerial 'managedAssignmentValid'); readbackValid=(Get-MarkerField $condemnedSerial 'managedReadbackValid'); objectBefore=(Get-MarkerField $condemnedSerial 'objectBefore'); objectAfter=(Get-MarkerField $condemnedSerial 'objectAfter'); objectAtStop=(Get-MarkerField $condemnedSerial 'objectAtStop'); duplicateObjectAddresses=(Get-MarkerField $condemnedSerial 'duplicateObjectAddresses'); objectHistoryOverflow=(Get-MarkerField $condemnedSerial 'objectHistoryOverflow') }
            rootArgument=[ordered]@{ slot=$firstCondemnedRun.root.slot; callbackRoot=$firstCondemnedRun.root.rawValue; membershipObject=$firstCondemnedRun.membership.object; heapResolutionInput=$firstCondemnedRun.heapResolution.input; condemnedCheckInput=$firstCondemnedRun.condemnedCheck.object; storageObject=$firstCondemnedRun.root.storageObject; identitiesEqual=($firstCondemnedRun.root.rawValue -eq $firstCondemnedRun.membership.object -and $firstCondemnedRun.membership.object -eq $firstCondemnedRun.heapResolution.input -and $firstCondemnedRun.heapResolution.input -eq $firstCondemnedRun.condemnedCheck.object -and $firstCondemnedRun.condemnedCheck.object -eq $firstCondemnedRun.root.storageObject) }
            membership=$firstCondemnedRun.membership
            wksHeap=[ordered]@{ multipleHeaps=$firstCondemnedRun.heapResolution.multipleHeaps; hpt=$firstCondemnedRun.heapResolution.hpt; heapOf=$firstCondemnedRun.heapResolution.heap; heapNumber=$firstCondemnedRun.heapResolution.heapNumber; heapCount=$firstCondemnedRun.heapResolution.heapCount; workstationSingleHeapSentinelValid=$firstCondemnedRun.heapResolution.singleHeapSentinelValid; heapResolutionFailures=$firstCondemnedRun.heapResolution.failures }
            sourceTrace=[ordered]@{ callbackFunction="WKS::GCHeap::Promote"; callbackSource="locked src/coreclr/gc/gc.cpp:49474-49544"; helperDeclaration="locked src/coreclr/gc/gcpriv.h:3054"; helperDefinition="locked src/coreclr/gc/gc.cpp:8389-8407"; helperContract="USE_REGIONS; object asserted in [g_gc_lowest_address,g_gc_highest_address); settings.condemned_generation compared with max_generation; get_region_gen_num(o) only when condemned_gen < max_generation"; helperFields=@("g_gc_lowest_address","g_gc_highest_address","settings.condemned_generation","max_generation","gc_heap::min_segment_size_shr","gc_heap::map_region_to_generation_skewed"); calledHelpers=@("get_region_gen_num(o)","get_skewed_basic_region_index_for_address(o)","region_of(o) in locked debug assertion"); generationDefinition="locked src/coreclr/gc/gc.cpp:12038-12045"; generationTableDefinition="locked src/coreclr/gc/gcpriv.h:1551-1576,4322-4326"; generationOf="not called"; findSegment="not called"; objectHeader="not read"; methodTable="not read"; objectDereference="none"; segmentLookup="one source-required region_of(o) debug assertion lookup in the proof build"; trueMeaning="region generation <= condemned generation, or full collection condemned_generation == max_generation"; falseMeaning="region generation > condemned generation"; trueNextSourceOperation="locked gc.cpp:49507 dprintf after the true branch"; falseNextSourceOperation="locked gc.cpp:49504 return"; firstPromotionMutation="locked gc.cpp:49541 hpt->mark_object_simple; not executed" }
            sourceInputs=$firstCondemnedRun.condemnedCheck
            machineCode=[ordered]@{ architecture="AMD64"; callingConvention="Microsoft x64 (RCX/RDX/R8; callback return boundary)"; callbackAddress="0x$callbackAddressText"; helper="inline USE_REGIONS sequence in GCHeap::Promote; no independent helper symbol expected"; disassembly=(Join-Path $runRoot "artifact-disassembly.txt"); callbackSymbolFile=(Join-Path $runRoot "callback-symbol.txt"); helperInputRegister="RCX/object is live through the inlined helper sequence"; comparison="lower/upper global-range assert, condemned_generation/max_generation compare, and generation <= condemned_generation compare"; selectedBranch=$firstCondemnedRun.condemnedCheck.branch; completionReturnAddress=(Get-MarkerField $condemnedSerial 'condemnedCompletionReturnAddress'); safeStopReturnAddress=(Get-MarkerField $condemnedSerial 'condemnedSafeStopReturnAddress') }
            generationState=[ordered]@{ condemnedGeneration=$firstCondemnedRun.condemnedCheck.condemnedGeneration; maximumGeneration=$firstCondemnedRun.condemnedCheck.maximumGeneration; generationFromRegion=$firstCondemnedRun.condemnedCheck.generation; generationTable=$firstCondemnedRun.condemnedCheck.generationTable; generationTableIndex=$firstCondemnedRun.condemnedCheck.generationTableIndex; segment=$firstCondemnedRun.condemnedCheck.segment; lowerBound=$firstCondemnedRun.condemnedCheck.lowerBound; upperBound=$firstCondemnedRun.condemnedCheck.upperBound; result=$firstCondemnedRun.condemnedCheck.result }
            counters=$firstCondemnedRun.counters
            mutation=$firstCondemnedRun.mutation
            execution=[ordered]@{ callbackReturns=(Get-MarkerField $condemnedSerial 'callbackReturns'); secondCallbacks=(Get-MarkerField $condemnedSerial 'secondCallbacks'); restartRequests=(Get-MarkerField $condemnedSerial 'restartRequests'); restartEntries=(Get-MarkerField $condemnedSerial 'restartEntries'); managedResume=(Get-MarkerField $condemnedSerial 'managedResume'); nextSourceOperation=$firstCondemnedRun.condemnedCheck.nextSourceOperation }
            threadStore=$firstCondemnedRun.threadStore
            scanContext=[ordered]@{ address=(Get-MarkerField $condemnedSerial 'callbackContext'); thread=(Get-MarkerField $condemnedSerial 'contextThread'); stackLimit=(Get-MarkerField $condemnedSerial 'contextStackLimit'); threadNumber=(Get-MarkerField $condemnedSerial 'contextThreadNumber'); threadCount=(Get-MarkerField $condemnedSerial 'contextThreadCount'); promotion=(Get-MarkerField $condemnedSerial 'contextPromotion'); concurrent=(Get-MarkerField $condemnedSerial 'contextConcurrent') }
            safeStop=[ordered]@{ marker="C011EC10"; reason="exactly one real condemned-generation decision for the genuine first root; stopped before promotion, marking, graph traversal, or mutation"; result=$firstCondemnedRun.condemnedCheck.result; nextSourceOperation=$firstCondemnedRun.condemnedCheck.nextSourceOperation; promotionAllowed=$false; markingAllowed=$false; graphTraversalAllowed=$false; mutationAllowed=$false }
            qemu=[ordered]@{ version=$qemuVersion; runCount=3; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); exactCommandLog=(Join-Path $runRoot "commands.txt"); runs=$runResults }
            regressions=[ordered]@{ c011ec10="PASS 3/3 fresh QEMU runs"; c011ec09="historical Outcome B retained; updated sentinel interpretation cross-referenced"; c011ec08="not rerun in this focused pass; historical PASS retained"; c011ec07="not rerun in this focused pass; historical PASS retained"; c011ec06="historical validator-clean result retained"; c011ec05="historical PASS retained"; primitiveThreadStatic="historical PASS retained"; referenceThreadStatic="historical PASS retained"; combinedThreadStatic="historical PASS retained"; c011ec03="historical validator-usable result retained"; c011ec02="historical validator-usable result retained"; c011ec01="historical validator-usable result retained"; staticChecks="PASS script parsing, manifest parsing, serial evidence, ordinary restoration, git diff --check" }
            retainedFailures=[ordered]@{ historicalFirst64KiBExecution="retained historical failure"; staleCacheAttempts="retained historical attempts"; initialRuntimePackIdentityMismatch="retained historical mismatch"; nativeStackPowerShellWrapper="retained NON-CLEAN exit 1; not relabeled PASS" }
            blockedNonClean=[ordered]@{ broadRegressionSuite="not rerun in this focused pass"; nativeStackWrapper="historically non-clean"; postCondemnedPromotion="intentionally blocked by the C011EC10 safe stop" }
            documentation=@("docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CONDEMNED_GENERATION_DECISION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_HEAP_RESOLUTION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CALLBACK_ENTRY.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_ROOT_CALLBACK_BOUNDARY.md","docs/dotnet/NATIVEAOT_THREAD_STATIC_RUNTIME_SUPPORT.md","docs/dotnet/NATIVEAOT_GC_STARTUP_READINESS.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FEASIBILITY.md")
            evidenceRoot=$runRoot; reportPath="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CONDEMNED_GENERATION_DECISION.md"; manifestPath=$manifestPath
            ordinaryRestoration=[ordered]@{ buildSha256=$normalKernelHash; espSha256=$normalKernelHash; expectedSha256=$normalKernelHash }
        }
        $manifest | ConvertTo-Json -Depth 28 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT Workstation GC first-root-condemned-generation-decision experiment: Outcome $outcome" -ForegroundColor Green
    } elseif ($isFirstRootHeapResolution) {
        $nonC011EC09Runs = @($runResults | Where-Object { $_["safeStopMarker"] -ne "C011EC09" })
        if (@($runResults).Count -ne 3 -or $nonC011EC09Runs.Count -ne 0) { throw "The first real root heap-resolution experiment did not produce three C011EC09 runs." }
        $firstHeapRun = $runResults[0]
        $outcome = if ($firstHeapRun.outcome -eq "A") { "A" } else { "B" }
        if (@($runResults | Where-Object { $_.outcome -ne $outcome }).Count -ne 0) { throw "Heap-resolution outcome was not deterministic across the three QEMU runs." }
        $heapSerial = Get-Content -LiteralPath $firstHeapRun.serial -Raw
        $manifest = [ordered]@{
            outcome="$outcome / first genuine NativeAOT root passed managed-range membership and completed exactly one real HEAP_FROM_THREAD/heap_of(o) resolution; stopped before generation logic"
            proofMode=$ProofMode; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            taskStartCheckpoint=$taskStartCheckpoint
            productionStartup=[ordered]@{ initializeModules="production runtime path invoked before ManagedMain"; threadStatic="real NativeAOT [ThreadStatic] provider"; status="working" }
            ordinaryBaseline=[ordered]@{ startingBuildSha256=$ordinaryBuildBefore; startingEspSha256=$ordinaryEspBefore; expectedSha256=$normalKernelHash }
            runtimePack=[ordered]@{ version="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterface="5.3"; ee="2"; sourceCommit=$lockedCommit; lockedGcenvEeSourceSha256=(Hash-File $lockedEePath); lockedGcEnumSourceSha256=(Hash-File $lockedGcEnumPath); lockedGcCppSourceSha256=(Hash-File $lockedGcCppPath); lockedGcwksSourceSha256=(Hash-File $lockedGcwksPath); activePalArchiveSha256=(Hash-File $activeArchive); callbackSymbol=$callbackSymbolName; callbackAddress="0x$callbackAddressText" }
            prerequisite=[ordered]@{ marker="C011EC08"; report="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION.md"; result="membership true for the genuine storage-object root; stopped before HEAP_FROM_THREAD" }
            managedProofRoot=[ordered]@{ field="[ThreadStatic] byte[]? s_gcProofThreadRoot"; sentinel=$firstHeapRun.root.sentinel; storageObject=$firstHeapRun.root.storage; inlinedRoot=(Get-MarkerField $heapSerial 'inlinedRoot'); assignmentCount=(Get-MarkerField $heapSerial 'managedAssignmentCount'); clearCount=(Get-MarkerField $heapSerial 'managedClearCount'); readbackCount=(Get-MarkerField $heapSerial 'managedReadbackCount'); assignmentValid=(Get-MarkerField $heapSerial 'managedAssignmentValid'); readbackValid=(Get-MarkerField $heapSerial 'managedReadbackValid'); objectBefore=(Get-MarkerField $heapSerial 'objectBefore'); objectAfter=(Get-MarkerField $heapSerial 'objectAfter'); objectAtStop=(Get-MarkerField $heapSerial 'objectAtStop'); duplicateObjectAddresses=(Get-MarkerField $heapSerial 'duplicateObjectAddresses'); objectHistoryOverflow=(Get-MarkerField $heapSerial 'objectHistoryOverflow') }
            rootArgument=[ordered]@{ slot=(Get-MarkerField $heapSerial 'rootSlot'); rawRoot=$firstHeapRun.root.loaded; callbackLoadedRoot=$firstHeapRun.root.loaded; membershipObject=$firstHeapRun.membership.object; heapResolutionInput=$firstHeapRun.heapResolution.input; expectedStorageObject=$firstHeapRun.root.storage; identitiesEqual=($firstHeapRun.root.loaded -eq $firstHeapRun.membership.object -and $firstHeapRun.membership.object -eq $firstHeapRun.heapResolution.input -and $firstHeapRun.heapResolution.input -eq $firstHeapRun.root.storage) }
            membership=[ordered]@{ requests=$firstHeapRun.membership.requests; entries=$firstHeapRun.membership.entries; completions=$firstHeapRun.membership.completions; object=$firstHeapRun.membership.object; lowerBound=$firstHeapRun.membership.lowerBound; upperBound=$firstHeapRun.membership.upperBound; lowerResult=(Get-MarkerField $heapSerial 'lowerResult'); upperResult=(Get-MarkerField $heapSerial 'upperResult'); result=$firstHeapRun.membership.result; objectDereferences=(Get-MarkerField $heapSerial 'membershipObjectDereferences'); segmentLookup="not part of membership helper" }
            sourceTrace=[ordered]@{ callbackFunction="WKS::GCHeap::Promote"; callbackSource="locked src/coreclr/gc/gc.cpp:49474-49544"; heapFromThread="locked src/coreclr/gc/gcpriv.h:315-326; Workstation branch at :326: gc_heap* hpt = 0"; heapOfDeclaration="locked src/coreclr/gc/gcpriv.h:2620"; heapOfDefinition="locked src/coreclr/gc/gc.cpp:26693-26707"; heapOfWorkstation="UNREFERENCED_PARAMETER(o); return __this;"; thisMacro="locked src/coreclr/gc/gcpriv.h:1540-1548; Workstation __this is (gc_heap*)0"; multipleHeaps="absent in Workstation; Server branch uses g_heaps[thread] and seg_mapping_table_heap_of(o)"; sourceFieldsConsulted=@("no object-address lookup", "no thread-state lookup", "no g_heaps table", "no segment map", "no brick/card bookkeeping", "no range fields"); sourceRequiredSegmentLookup=0; sourceRequiredObjectDereference=0; nextSemanticOperation="locked gc.cpp:49498-49505; active USE_REGIONS operation gc_heap::is_in_condemned_gc(o) at :49499"; laterNonRegionsOperation="locked gc.cpp:49501 object-range comparison hp->gc_low/gc_high"; generatedGcwks=$gcWksSource; generatedGcEnum=$gcEnumSource }
            heapResolution=$firstHeapRun.heapResolution
            heapRelationship=[ordered]@{ workstationLogicalHeapCount=$firstHeapRun.heapResolution.totalHeapCount; returnedHeap=$firstHeapRun.heapResolution.pointer; returnedHeapNumber=$firstHeapRun.heapResolution.heapNumber; threadHeap=$firstHeapRun.heapResolution.threadHeap; heapTable="not applicable in Workstation"; segment="not consulted; segment identification is out of scope"; matchesSohOwner="not evaluated because source heap_of has no segment lookup and returned null" }
            machineCode=[ordered]@{ architecture="AMD64"; callingConvention="Microsoft x64; RCX/RDX/R8"; callbackAddress="0x$callbackAddressText"; disassembly=(Join-Path $runRoot "artifact-disassembly.txt"); callbackSymbolFile=(Join-Path $runRoot "callback-symbol.txt"); membershipBranch="recorded in artifact disassembly around GCHeap::Promote"; heapFromThread="first proof hook immediately after source HEAP_FROM_THREAD; hpt input is source-produced zero"; heapOf="inline sequence in GCHeap::Promote; source result passed to completion observer"; nextGenerationInstruction="not executed; source next is gc_heap::is_in_condemned_gc(o) at locked gc.cpp:49499" }
            prohibitedOperations=[ordered]@{ generationClassificationStart=(Get-MarkerField $heapSerial 'generationClassificationStart'); generationQueryStart=(Get-MarkerField $heapSerial 'generationQueryStart'); condemnedGenerationComparisons=(Get-MarkerField $heapSerial 'condemnedGenerationComparisons'); ephemeralGenerationComparisons=(Get-MarkerField $heapSerial 'ephemeralGenerationComparisons'); segmentLookup=(Get-MarkerField $heapSerial 'postResolutionSegmentLookup'); objectHeaders=(Get-MarkerField $heapSerial 'objectHeaders'); methodTables=(Get-MarkerField $heapSerial 'methodTables'); childReferences=(Get-MarkerField $heapSerial 'childReferenceReads'); promotionStart=(Get-MarkerField $heapSerial 'promotionStart'); promotion=(Get-MarkerField $heapSerial 'promotions'); markingStart=(Get-MarkerField $heapSerial 'markingStart'); graphTraversal=(Get-MarkerField $heapSerial 'graphTraversal') }
            mutation=[ordered]@{ mark=(Get-MarkerField $heapSerial 'markWrites'); promotion=(Get-MarkerField $heapSerial 'promotionWrites'); object=(Get-MarkerField $heapSerial 'objectMutation'); gcMetadata=(Get-MarkerField $heapSerial 'gcMetadataMutation'); segment=(Get-MarkerField $heapSerial 'segmentMetadataMutation') }
            scanContext=$firstHeapRun.scanContext
            threadStore=$firstHeapRun.threadStore
            execution=[ordered]@{ callbackReturns=(Get-MarkerField $heapSerial 'callbackReturns'); secondCallbacks=(Get-MarkerField $heapSerial 'secondCallbacks'); restartRequests=(Get-MarkerField $heapSerial 'restartRequests'); restartEntries=(Get-MarkerField $heapSerial 'restartEntries'); managedResume=(Get-MarkerField $heapSerial 'managedResume') }
            safeStop=[ordered]@{ marker="C011EC09"; reason="the genuine first root passed true managed-range membership and exactly one real HEAP_FROM_THREAD/heap_of(o) transition; stopped before gc_heap::is_in_condemned_gc(o)"; nextSourceOperation="gc.cpp:49499"; outcome=$outcome }
            qemu=[ordered]@{ version=$qemuVersion; runCount=3; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); exactCommandLog=(Join-Path $runRoot "commands.txt"); runs=$runResults }
            regressions=[ordered]@{ c011ec08="historical PASS 3/3 retained"; c011ec07="historical PASS 3/3 retained"; c011ec06="historical validator-clean result retained"; c011ec05="historical PASS retained"; primitiveThreadStatic="historical PASS retained"; referenceThreadStatic="historical PASS retained"; combinedThreadStatic="historical PASS retained"; c011ec03="historical validator-usable result retained"; c011ec02="historical validator-usable result retained"; c011ec01="historical validator-usable result retained"; broadRegressionSuite="not rerun in this focused pass"; staticChecks="script parsing, manifest parsing, serial evidence, ordinary restoration, git diff --check" }
            retainedFailures=[ordered]@{ historicalFirst64KiBExecution="retained historical failure"; staleCacheAttempts="retained historical attempts"; initialRuntimePackIdentityMismatch="retained historical mismatch"; nativeStackPowerShellWrapper="retained NON-CLEAN exit 1; not relabeled PASS" }
            blockedNonClean=[ordered]@{ broadRegressionSuite="blocked/not rerun in focused pass"; nativeStackWrapper="historically non-clean"; heapOwnerSegmentEquality="not evaluated because heap_of returned Workstation null sentinel and segment lookup is out of scope" }
            documentation=@("docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_HEAP_RESOLUTION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CALLBACK_ENTRY.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_ROOT_CALLBACK_BOUNDARY.md","docs/dotnet/NATIVEAOT_THREAD_STATIC_RUNTIME_SUPPORT.md","docs/dotnet/NATIVEAOT_GC_STARTUP_READINESS.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FEASIBILITY.md")
            evidenceRoot=$runRoot; reportPath="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_HEAP_RESOLUTION.md"; manifestPath=$manifestPath
        }
        $manifest | ConvertTo-Json -Depth 24 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT Workstation GC first-root-heap-resolution experiment: Outcome $outcome" -ForegroundColor Green
    } elseif ($isFirstRootMembershipClassification) {
        $nonC011EC08Runs = @($runResults | Where-Object { $_["safeStopMarker"] -ne "C011EC08" })
        if (@($runResults).Count -ne 3 -or $nonC011EC08Runs.Count -ne 0) { throw "The first real root membership experiment did not produce three C011EC08 runs." }
        $firstMembershipRun = $runResults[0]
        $manifest = [ordered]@{
            outcome="A / the genuine NativeAOT thread-static storage root completed exactly one Workstation GC managed-range membership check with a true result and stopped before generation classification"
            proofMode=$ProofMode; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            taskStartCheckpoint=$taskStartCheckpoint
            productionStartup=[ordered]@{ initializeModules="production runtime path invoked before ManagedMain"; status="working" }
            ordinaryBaseline=[ordered]@{ startingBuildSha256=$ordinaryBuildBefore; startingEspSha256=$ordinaryEspBefore; expectedSha256=$normalKernelHash }
            runtimePack=[ordered]@{ version="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterface="5.3"; ee="2"; sourceCommit=$lockedCommit; lockedGcenvEeSourceSha256=(Hash-File $lockedEePath); lockedGcEnumSourceSha256=(Hash-File $lockedGcEnumPath); lockedGcCppSourceSha256=(Hash-File $lockedGcCppPath); lockedGcwksSourceSha256=(Hash-File $lockedGcwksPath); activePalArchiveSha256=(Hash-File $activeArchive); callbackSymbol=$callbackSymbolName; callbackAddress="0x$callbackAddressText"; membershipHelper="gc_heap::is_in_find_object_range"; membershipHelperAddress="inline; no out-of-line helper symbol in the final map" }
            priorCheckpoint=[ordered]@{ marker="C011EC07"; report="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CALLBACK_ENTRY.md"; stop="real GCHeap::Promote entered, loaded the genuine storage-object root, and stopped before is_in_find_object_range" }
            managedProofRoot=$firstMembershipRun.managedProofRoot
            threadStaticRuntime=[ordered]@{ storageObjectAddress=$firstMembershipRun.managedProofRoot.storageObject; inlinedRootAddress=(Get-MarkerField (Get-Content -LiteralPath $firstMembershipRun.serial -Raw) 'inlinedRoot'); normalManagedSemantics=$true; fabricatedSlot=$false; fabricatedObject=$false }
            sourceTrace=[ordered]@{ callbackTypedef="promote_func(Object**, ScanContext*, uint32_t)"; callbackFunction="WKS::GCHeap::Promote"; callbackSource="locked src/coreclr/gc/gc.cpp:49474-49544, included by src/coreclr/gc/gcwks.cpp"; membershipDeclaration="locked src/coreclr/gc/gcpriv.h:3049"; membershipDefinition="locked src/coreclr/gc/gc.cpp:8363-8387"; activeConfiguration="USE_REGIONS from gcpriv.h:147-149 and FEATURE_CONSERVATIVE_GC"; activeExpression="gc.cpp:8373: (o >= g_gc_lowest_address) && (o < bookkeeping_covered_committed), after gc.cpp:8368-8371 null guard"; rangeFields=@("g_gc_lowest_address", "gc_heap::bookkeeping_covered_committed"); heapIdentity="none; helper reads no heap identity"; segmentLookup="none"; objectDereference="none"; callbackGeneration="locked gc.cpp:29899-29901: GCScan::GcScanRoots(GCHeap::Promote, ...)"; rootProvider="locked src/coreclr/nativeaot/Runtime/gcenv.ee.cpp:114-115"; enumGcRef="locked src/coreclr/nativeaot/Runtime/GcEnum.cpp:68-96"; callbackCallSite="locked GcEnum.cpp:81: fnGcEnumRef(ppObj, pSc, flags)"; firstCandidateLoad="locked gc.cpp:49481: uint8_t* o = (uint8_t*)*ppObject"; trueNextSourceOperation="locked gc.cpp:49494: HEAP_FROM_THREAD, after the DEBUG_DestroyedHandleValue block is compiled out"; falseNextSourceOperation="locked gc.cpp:49485: return; (the proof boundary executes before that return and does not skip the root)"; postMembershipGenerationStart="locked gc.cpp:49496 heap_of(o) and gc.cpp:49499-49505 condemned-generation/range logic"; firstMetadataAccess="locked gc.cpp:49521 CObjectHeader::IsFree in conservative branch or gc.cpp:49528 Validate in debug"; firstPromotionMutation="locked gc.cpp:49533 pin_object / gc.cpp:49541 mark_object_simple"; generatedGcwks=$gcWksSource; generatedGcEnum=$gcEnumSource }
            machineCode=[ordered]@{ architecture="AMD64"; callingConvention="Microsoft x64"; callbackAddress="0x$callbackAddressText"; callbackEntry="map symbol ?Promote@GCHeap@WKS@@SAXPEAPEAVObject@@PEAUScanContext@@I@Z"; membershipHelper="inline; no out-of-line address"; membershipHelperSource="gc.cpp:8363-8387"; membershipSequence="final artifact disassembly around GCHeap::Promote shows the two range loads/comparisons and the completion observer call"; membershipLowerComparison="0x1002461D cmp rbx,rdx after 0x10024613 load of g_gc_lowest_address; 0x10024631 setae r10b"; membershipUpperComparison="0x1002462D load of bookkeeping_covered_committed; 0x10024635 setb al"; membershipResultBranch="0x1002467C test dil; 0x10024684 je false boundary; 0x1002468B true boundary call"; completionObserverReturnAddress=$firstMembershipRun.membership.completionReturnAddress; postMembershipBoundaryReturnAddress=$firstMembershipRun.membership.postCheckReturnAddress; disassembly=(Join-Path $runRoot "artifact-disassembly.txt"); callbackSymbolFile=(Join-Path $runRoot "callback-symbol.txt") }
            callbackCallSite=$firstMembershipRun.callbackSite
            callbackEntry=$firstMembershipRun.callbackEntry
            rootArgument=$firstMembershipRun.root
            scanContext=$firstMembershipRun.scanContext
            membershipCheck=$firstMembershipRun.membership
            heapRange=[ordered]@{ heapIdentity=(Get-MarkerField (Get-Content -LiteralPath $firstMembershipRun.serial -Raw) 'heapIdentity'); heapFieldReads=(Get-MarkerField (Get-Content -LiteralPath $firstMembershipRun.serial -Raw) 'heapFieldReads'); lowerBound=$firstMembershipRun.membership.lowerBound; upperBound=$firstMembershipRun.membership.upperBound; lowerComparison=$firstMembershipRun.membership.lowerResult; upperComparison=$firstMembershipRun.membership.upperResult; finalResult=$firstMembershipRun.membership.result; sourceBranch=$firstMembershipRun.membership.sourceBranch; sourceRequiredSegmentLookupCount=(Get-MarkerField (Get-Content -LiteralPath $firstMembershipRun.serial -Raw) 'segmentLookupCount'); sourceRequiredSegmentLookupSucceeded=(Get-MarkerField (Get-Content -LiteralPath $firstMembershipRun.serial -Raw) 'segmentLookupSucceeded') }
            prohibitedOperations=$firstMembershipRun.prohibited
            mutation=$firstMembershipRun.mutation
            objectAndSentinelValidation=$firstMembershipRun.managedProofRoot
            threadStore=$firstMembershipRun.threadStore
            safeStop=[ordered]@{ marker="C011EC08"; reason="the real first root completed exactly one is_in_find_object_range check with true result; stopped before HEAP_FROM_THREAD/generation classification"; firstPostMembershipOperation="gc.cpp:49494 HEAP_FROM_THREAD"; promotionAllowed=$false; markingAllowed=$false; graphTraversalAllowed=$false; callbackReturnAllowed=$false; managedResumeAllowed=$false }
            qemu=[ordered]@{ version=$qemuVersion; runCount=3; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); exactCommandLog=(Join-Path $runRoot "commands.txt"); runs=$runResults }
            regressions=[ordered]@{ firstRootMembershipClassification="PASS 3/3 fresh QEMU runs"; firstRootCallbackEntry="C011EC07 regression retained; new run also entered the same callback"; firstNonNullRootCallbackBoundary="C011EC06 prerequisite retained"; firstRootCandidateLoad="C011EC05 prerequisite retained"; threadStaticPrimitive="historical proof retained"; threadStaticReference="historical proof retained"; threadStaticCombined="historical proof retained"; firstPerThreadRootProvider="C011EC04 prerequisite retained"; allocationContextFixupRootBoundary="C011EC03 prerequisite retained"; singleThreadSuspendEe="C011EC02 prerequisite retained"; staticChecks="PASS script parse, manifest parse, serial classification, ordinary-kernel restoration, git diff --check" }
            failedChecks=[ordered]@{ historicalFirst64KiBExecution="retained historical failure"; staleCacheAttempts="retained historical attempts"; initialRuntimePackIdentityMismatch="retained historical mismatch"; nativeStackPowerShellWrapper="retained NON-CLEAN exit 1 from compiler-stderr promotion" }
            blockedChecks=[ordered]@{ broadRegressionSuite="not rerun in this focused execution; historical manifests retained without relabeling"; nativeStackWrapper="historically non-clean; not called passed"; promotionCompletion="intentionally out of scope; proof stops before generation classification/promotion" }
            documentation=@("docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CALLBACK_ENTRY.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_ROOT_CALLBACK_BOUNDARY.md","docs/dotnet/NATIVEAOT_THREAD_STATIC_RUNTIME_SUPPORT.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CANDIDATE_LOAD.md","docs/dotnet/NATIVEAOT_GC_STARTUP_READINESS.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FEASIBILITY.md"); evidenceRoot=$runRoot; reportPath="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION.md"
            ordinaryRestoration=[ordered]@{ buildSha256=$normalKernelHash; espSha256=$normalKernelHash; expectedSha256=$normalKernelHash }
            authorizedNormalizedAdaptedGcIdentity=[ordered]@{ strategy=$identity.strategy; sourceCommit=$identity.sourceCommit; stockSha256=$identity.stockSha256; adaptedSha256=$identity.adaptedSha256; adaptedLength=$identity.adaptedLength; replacementObjects=$replacementHashes; stockUnchanged=$identity.stockUnchanged }
        }
        $manifest | ConvertTo-Json -Depth 28 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT Workstation GC first-root-membership-classification experiment: PASS (Outcome A)" -ForegroundColor Green
    } elseif ($isFirstRootFirstNonNullOldO) {
        $allNonNullOldOBlockedRuns = @($runResults).Count -eq $FreshBootCount -and
            @($runResults | Where-Object { $_.outcome -eq "E" -and $_.earlyFailure -eq "c011ec14-no-non-null-old-o-before-nativeaot-failfast" }).Count -eq $FreshBootCount
        if ($allNonNullOldOBlockedRuns) {
            $firstBlockedRun = $runResults[0]
            $manifest = [ordered]@{
                outcome="D / the valid bounded workload reached one real queue insertion and the first null old_o decision, then the source-valid route to natural displacement required a new semantic unit before another queue insertion; no naturally valid non-null old_o or marked(old_o) read was obtained"
                proofMode=$ProofMode; marker="C011EC14 not reached"; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
                taskStartCheckpoint=$taskStartCheckpoint
                lockedRuntimeIdentity=[ordered]@{ nativeAot="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterfaces="5.3 / 2"; sourceCommit=$lockedCommit; productionInitializeModules=$true; productionThreadStatic=$true }
                ordinaryBaseline=[ordered]@{ startingBuildSha256=$ordinaryKernelBefore.build; startingEspSha256=$ordinaryKernelBefore.esp; expectedSha256=$normalKernelHash }
                priorCheckpoint=[ordered]@{ marker="C011EC13"; commit="684b6fb507e4158191e52489af32a894dde8fc75"; report="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_POST_QUEUE_MARK_DECISION.md"; outcome="first valid queue_mark old_o was null" }
                workload=[ordered]@{ managedMode="FirstNonNullOldO"; rootField="[ThreadStatic] byte[]? s_gcProofThreadRoot"; additionalRoots="none retained; ordinary locals and GC handles did not produce a candidate before the bounded fail-fast"; fabricatedSlot=$false; fabricatedObject=$false; graphTraversal=$false }
                root=$firstBlockedRun.root
                queue=$firstBlockedRun.queue
                decision=$firstBlockedRun.decision
                markState=$firstBlockedRun.markState
                sourceTrace=[ordered]@{ promote="locked src/coreclr/gc/gc.cpp:49474-49544"; markObjectSimple="locked gc.cpp:27987-28029; first queue_mark call at :28007"; queueDeclaration="locked gcpriv.h:1487-1504; slot_table[16] and curr_slot_index"; queueDefinition="locked gc.cpp:27303-27335; old_o capture at :27316-27317, slot write at :27318, cursor advance at :27320, null return at :27321-27322, marked(old_o) at :27328, set_marked(old_o) at :27333"; markedRead="not reached"; nextRequiredBoundary="a source-valid route that supplies additional real queue insertions before the runtime continuation/fail-fast boundary" }
                mutation=[ordered]@{ prerequisiteQueueSlotWrites="0x00000001"; prerequisiteCursorWrites="0x00000001"; markHeaderReads="0x00000000"; markHeaderWrites="0x00000000"; otherObjectHeaderWrites="0x00000000"; segmentWrites="0x00000000"; worklistWrites="0x00000001"; newC011EC14Mutation="0x00000000" }
                traversal=[ordered]@{ graphTraversal="0x00000000 observed before fail-fast"; childReferenceReads="0x00000000"; childObjects="0x00000000"; secondObjectMarkAttempts="0x00000000" }
                threadStore=[ordered]@{ lockHeld="0x00000001"; eeSuspended="0x00000001"; managedEntryProhibited="0x00000001"; restart="0x00000000"; managedResume="0x00000000" }
                qemu=[ordered]@{ version=$qemuVersion; runCount=$FreshBootCount; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); exactCommandLog=(Join-Path $runRoot "commands.txt"); runs=$runResults }
                regressions=[ordered]@{ c011ec14="Outcome D $FreshBootCount/$FreshBootCount fresh QEMU 11.0.0 boots; deterministic bounded stop before non-null displacement"; c011ec13="PASS prerequisite checkpoint retained"; c011ec12="PASS prerequisite checkpoint retained"; c011ec10="PASS/source prerequisite retained"; staticChecks="PASS script parse, manifest parse, serial classification, ordinary restoration, git diff --check" }
                retainedFailures=[ordered]@{ historicalFirst64KiBExecution="retained historical mismatch"; staleCacheAttempts="retained historical attempts"; initialRuntimePackIdentityMismatch="retained historical mismatch"; nativeStackPowerShellWrapper="retained NON-CLEAN historical evidence" }
                blockedNonClean=[ordered]@{ nonNullOldO="blocked before naturally valid occupied displacement"; broadRegressionSuite="not rerun as one combined suite" }
                documentation=@("docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_OLD_O_MARK_DECISION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_POST_QUEUE_MARK_DECISION.md")
                evidenceRoot=$runRoot; reportPath="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_OLD_O_MARK_DECISION.md"; manifestPath=$manifestPath; ordinaryRestoration=[ordered]@{ buildSha256=$normalKernelHash; espSha256=$normalKernelHash; expectedSha256=$normalKernelHash }
            }
            $manifest | ConvertTo-Json -Depth 32 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
            Write-Host "NativeAOT Workstation GC first-root-first-non-null-old-o experiment: Outcome D" -ForegroundColor Yellow
        } else {
        $nonNullOldORuns = @($runResults | Where-Object { $_["safeStopMarker"] -ne "C011EC14" })
        if (@($runResults).Count -ne 3 -or $nonNullOldORuns.Count -ne 0) { throw "The C011EC14 experiment did not produce three C011EC14 runs." }
        $firstNonNullOldORun = $runResults[0]
        $manifest = [ordered]@{
            outcome="A / genuine NativeAOT Workstation GC queue_mark naturally wrapped its 16-slot ring; the first non-null displaced old_o completed the actual marked(old_o) read/decision and stopped before set_marked(old_o)"
            proofMode=$ProofMode; marker="C011EC14"; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            taskStartCheckpoint=$taskStartCheckpoint
            lockedRuntimeIdentity=[ordered]@{ nativeAot="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterfaces="5.3 / 2"; sourceCommit=$lockedCommit; productionInitializeModules=$true; productionThreadStatic=$true }
            ordinaryBaseline=[ordered]@{ startingBuildSha256=$ordinaryKernelBefore.build; startingEspSha256=$ordinaryKernelBefore.esp; expectedSha256=$normalKernelHash }
            priorCheckpoint=[ordered]@{ marker="C011EC13"; commit="684b6fb507e4158191e52489af32a894dde8fc75"; report="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_POST_QUEUE_MARK_DECISION.md"; outcome="first valid queue_mark old_o was null" }
            workload=[ordered]@{ managedMode="FirstNonNullOldO"; rootFields=17; queueCapacity=16; ordinaryManagedRoots=$true; fabricatedSlot=$false; fabricatedObject=$false; graphTraversal=$false }
            root=$firstNonNullOldORun.root
            queue=$firstNonNullOldORun.queue
            decision=$firstNonNullOldORun.decision
            markState=$firstNonNullOldORun.markState
            provenance=$firstNonNullOldORun.provenance
            counters=$firstNonNullOldORun.counters
            mutation=$firstNonNullOldORun.mutation
            traversal=$firstNonNullOldORun.traversal
            sourceTrace=[ordered]@{ promote="locked src/coreclr/gc/gc.cpp:49474-49544"; markObjectSimple="locked gc.cpp:27987-28029; loads settings.condemned_generation then *po and calls mark_queue.queue_mark(o)"; queueDeclaration="locked gcpriv.h:1487-1504; slot_table[16] and curr_slot_index"; queueDefinition="locked gc.cpp:27303-27335; old_o capture at :27316-27317, slot write at :27318, cursor advance at :27320, null return at :27321-27322, marked(old_o) at :27328, set_marked(old_o) at :27333"; markedMacro="locked gc.cpp:11587: marked(i) -> header(i)->IsMarked()"; markedRead="locked gc.cpp:4789-4792: CObjectHeader::IsMarked reads RawGetMethodTable() and tests GC_MARKED"; stop="after the actual marked(old_o) result and before the locked set_marked(old_o) mutation" }
            safeStop=[ordered]@{ marker="C011EC14"; reason="first non-null displaced old_o completed actual mark-state read/decision; no mark-bit write, traversal, callback completion, restart, or resume"; callbackReturnsBeforeDecision=$firstNonNullOldORun.counters.callbackReturnsBeforeDecision; restartAllowed=$false; resumeAllowed=$false }
            qemu=[ordered]@{ version=$qemuVersion; runCount=3; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); exactCommandLog=(Join-Path $runRoot "commands.txt"); runs=$runResults }
            regressions=[ordered]@{ c011ec14="PASS 3/3 fresh QEMU 11.0.0 boots"; c011ec13="PASS prerequisite checkpoint retained"; c011ec12="PASS prerequisite checkpoint retained"; c011ec10="source/path prerequisite retained"; historicalSuite="retained without relabeling"; staticChecks="PASS script parse, manifest parse, serial evidence, ordinary restoration, git diff --check" }
            retainedFailures=[ordered]@{ historicalFirst64KiBExecution="retained historical mismatch"; staleCacheAttempts="retained historical attempts"; initialRuntimePackIdentityMismatch="retained historical mismatch"; nativeStackPowerShellWrapper="retained NON-CLEAN historical evidence" }
            blockedNonClean=[ordered]@{ broadRegressionSuite="not rerun as one combined suite"; markingCompletion="out of scope; proof stops before set_marked(old_o)" }
            documentation=@("docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_OLD_O_MARK_DECISION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_POST_QUEUE_MARK_DECISION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_FIRST_MARK_MUTATION.md")
            evidenceRoot=$runRoot; reportPath="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_OLD_O_MARK_DECISION.md"; manifestPath=$manifestPath; ordinaryRestoration=[ordered]@{ buildSha256=$normalKernelHash; espSha256=$normalKernelHash; expectedSha256=$normalKernelHash }
        }
        $manifest | ConvertTo-Json -Depth 32 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT Workstation GC first-root-first-non-null-old-o experiment: Outcome A" -ForegroundColor Green
        }
    } elseif ($isFirstRootPostQueueMarkDecision) {
        $nonPostQueueRuns = @($runResults | Where-Object { $_["safeStopMarker"] -ne "C011EC13" })
        if (@($runResults).Count -ne 3 -or $nonPostQueueRuns.Count -ne 0) { throw "The C011EC13 experiment did not produce three C011EC13 runs." }
        $firstPostQueueRun = $runResults[0]
        $postQueueOutcome = $firstPostQueueRun.outcome
        if (@($runResults | Where-Object { $_.outcome -ne $postQueueOutcome }).Count -ne 0) { throw "C011EC13 post-queue outcome was not deterministic across the three QEMU runs." }
        $postQueueSerial = Get-Content -LiteralPath $firstPostQueueRun.serial -Raw
        $postQueueMachineCodePath = Join-Path $runRoot 'post-queue-machine-code.txt'
        Require-File $postQueueMachineCodePath "C011EC13 machine-code evidence"
        $postQueueMachineCode = Get-Content -LiteralPath $postQueueMachineCodePath -Raw
        $getPostQueueMachineField = {
            param([string]$Name)
            $match = [regex]::Match($postQueueMachineCode, '(?im)^' + [regex]::Escape($Name) + '=(?<value>.*)$')
            if ($match.Success) { return $match.Groups['value'].Value.Trim() }
            return $null
        }
        $manifest = [ordered]@{
            outcome="$postQueueOutcome / genuine NativeAOT root completed the real queue_mark old_o null/mark-state decision; stopped before later mutation or traversal"
            proofMode=$ProofMode; marker="C011EC13"; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            taskStartCheckpoint=$taskStartCheckpoint
            lockedRuntimeIdentity=[ordered]@{ nativeAot="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterfaces="5.3 / 2"; sourceCommit=$lockedCommit; gcCppSha256=(Hash-File $lockedGcCppPath); gcprivSha256=(Hash-File $lockedGcPrivPath); gcwksSha256=(Hash-File $lockedGcwksPath); productionInitializeModules=$true; productionThreadStatic=$true }
            ordinaryBaseline=[ordered]@{ startingBuildSha256=$ordinaryKernelBefore.build; startingEspSha256=$ordinaryKernelBefore.esp; expectedSha256=$normalKernelHash }
            priorCheckpoint=[ordered]@{ marker="C011EC12"; report="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_FIRST_MARK_MUTATION.md"; stop="after the authentic slot/cursor queue invariant and before old_o decision" }
            proofRoot=$firstPostQueueRun.root; findRange=[ordered]@{ result="0x00000001"; rawRoot=$firstPostQueueRun.root.rawValue; membership=(Get-MarkerField $postQueueSerial 'membership') }
            workstationHeap=[ordered]@{ multipleHeaps=(Get-MarkerField $postQueueSerial 'wksMultipleHeaps'); hpt=(Get-MarkerField $postQueueSerial 'hpt'); heapOf=(Get-MarkerField $postQueueSerial 'heapOf'); heapNumber=(Get-MarkerField $postQueueSerial 'heapNumber'); heapCount=(Get-MarkerField $postQueueSerial 'heapCount'); nullSentinelValid=(Get-MarkerField $postQueueSerial 'wksNullHeapValid') }
            condemnedGeneration=[ordered]@{ result=(Get-MarkerField $postQueueSerial 'condemned'); generation=(Get-MarkerField $postQueueSerial 'generationFromRegion'); condemnedGeneration=(Get-MarkerField $postQueueSerial 'condemnedGeneration'); maximumGeneration=(Get-MarkerField $postQueueSerial 'maximumGeneration') }
            queueMark=[ordered]@{ declaration="gcpriv.h:2729 mark_object_simple; gcpriv.h:1487-1504 mark_queue_t"; definition="gc.cpp:27987-28029 mark_object_simple; gc.cpp:27303-27335 queue_mark"; slotAddress=$firstPostQueueRun.queue.slotAddress; slotIndex=$firstPostQueueRun.queue.slotIndex; oldSlot=$firstPostQueueRun.queue.oldSlot; newSlot=$firstPostQueueRun.queue.newSlot; oldObject=$firstPostQueueRun.queue.oldObject; oldObjectMeaning="previous contents of slot_table[slot_index], captured before insertion"; cursorBefore=$firstPostQueueRun.queue.cursorBefore; cursorAfter=$firstPostQueueRun.queue.cursorAfter; capacity="0x0000000000000010"; inheritedSlotWrites=$firstPostQueueRun.mutation.inheritedQueueSlotWrites; inheritedCursorWrites=$firstPostQueueRun.mutation.inheritedCursorWrites }
            sourceTrace=[ordered]@{ markObjectSimple="gc.cpp:27987-28029; first reads condemned generation :27991-27996 and *po :27998; delegates at :28007"; queueDeclaration="gcpriv.h:1487-1504; slot_table[16] and curr_slot_index"; queueDefinition="gc.cpp:27303-27335; one-argument queue_mark"; queueWrites="gc.cpp:27316-27320 old_o capture, slot_table write, cursor advance"; nullBranch="gc.cpp:27321-27322 if(old_o==nullptr) return nullptr"; markedMacro="gc.cpp:11587 #define marked(i) header(i)->IsMarked()"; markedImplementation="gc.cpp:4789-4792 CObjectHeader::IsMarked reads GC_MARKED from RawGetMethodTable"; markRepresentation="object-header raw method-table-word GC_MARKED bit"; markedMutates=$false; markedDereferences="only non-null path; skipped for null old_o"; setMarked="gc.cpp:27333 set_marked(old_o), not reached"; childTraversal="gc.cpp:28014 go_through_object_cl, not reached"; nextMutation="inline set_marked(old_o) at gc.cpp:27333 on non-null/unmarked path; not executed" }
            decision=$firstPostQueueRun.decision; markState=[ordered]@{ representation="CObjectHeader::IsMarked / GC_MARKED"; reads=$firstPostQueueRun.decision.markStateReads; result=$firstPostQueueRun.decision.markedResult; objectHeaderReads=(Get-MarkerField $postQueueSerial 'objectHeaderReads'); methodTableReads=(Get-MarkerField $postQueueSerial 'methodTableReads'); segmentReads=(Get-MarkerField $postQueueSerial 'segmentReads'); regionReads=(Get-MarkerField $postQueueSerial 'regionReads') }
            mutation=[ordered]@{ inheritedFromC011EC12=2; inheritedQueueSlotWrites=$firstPostQueueRun.mutation.inheritedQueueSlotWrites; inheritedCursorWrites=$firstPostQueueRun.mutation.inheritedCursorWrites; newPostQueueAttempts=$firstPostQueueRun.mutation.newAttempts; newPostQueueExecutions=$firstPostQueueRun.mutation.newExecutions; markBitWrites=$firstPostQueueRun.mutation.markBitWrites; logicalMarkComplete=(Get-MarkerField $postQueueSerial 'logicalMarkComplete'); objectHeaderWrites=0; payloadWrites=0; segmentWrites=0; regionWrites=0; overflowWrites=0; traversalWorkItemWrites=0 }
             traversal=$firstPostQueueRun.traversal; nextMutationBoundary=[ordered]@{ helper="inline mark_queue_t::queue_mark set_marked(old_o)"; source="locked gc.cpp:27333; machine evidence identifies the following inline OR on the old object's raw method-table word"; instruction=(& $getPostQueueMachineField 'nextMutationInstruction'); machineInstruction=(& $getPostQueueMachineField 'nextMutationInstruction'); machineLine=(& $getPostQueueMachineField 'nextMutationLine'); decisionReturnAddress=$firstPostQueueRun.decision.decisionReturnAddress; safeStopAddress=$firstPostQueueRun.decision.safeStopAddress; executed=$false }
            threadStore=$firstPostQueueRun.threadStore; execution=[ordered]@{ callbackReturns=(Get-MarkerField $postQueueSerial 'callbackReturns'); secondCallbacks=(Get-MarkerField $postQueueSerial 'secondCallbacks'); restart=(Get-MarkerField $postQueueSerial 'restart'); resume=(Get-MarkerField $postQueueSerial 'resume') }; objectAndSentinelValidation=$firstPostQueueRun.validation
            machineCode=[ordered]@{ architecture="AMD64"; callingConvention="Microsoft x64"; helperSymbol="?mark_object_simple@gc_heap@WKS@@CAXPEAPEAE@Z"; helperAddress=(Get-MarkerField $postQueueSerial 'markHelper'); inheritedQueueSlotWriteInstruction=(& $getPostQueueMachineField 'inheritedQueueSlotWriteInstruction'); inheritedQueueSlotWriteLine=(& $getPostQueueMachineField 'inheritedQueueSlotWriteLine'); inheritedCursorWriteInstruction=(& $getPostQueueMachineField 'inheritedCursorWriteInstruction'); inheritedCursorWriteLine=(& $getPostQueueMachineField 'inheritedCursorWriteLine'); nullDecisionHelper=(& $getPostQueueMachineField 'nullDecisionHelper'); nullDecisionHelperAddress=(& $getPostQueueMachineField 'nullDecisionHelperAddress'); nullDecisionCallSite=(& $getPostQueueMachineField 'nullDecisionCallSite'); nullDecisionCallLine=(& $getPostQueueMachineField 'nullDecisionCallLine'); nullTestInstruction=(& $getPostQueueMachineField 'nullTestInstruction'); nullBranchInstruction=(& $getPostQueueMachineField 'nullBranchInstruction'); markedRepresentation=(& $getPostQueueMachineField 'markedRepresentation'); markedOutOfLineHelper=(& $getPostQueueMachineField 'markedOutOfLineHelper'); nextMutationInstruction=(& $getPostQueueMachineField 'nextMutationInstruction'); nextMutationLine=(& $getPostQueueMachineField 'nextMutationLine'); evidence=$postQueueMachineCodePath; disassembly=(Join-Path $runRoot 'artifact-disassembly.txt') }
            safeStop=[ordered]@{ marker="C011EC13"; classification=$postQueueOutcome; reason="one real old_o null/marked decision completed after C011EC12 queue unit; stopped before callback return, later mutation, traversal, or second object"; childTraversalAllowed=$false; secondObjectAllowed=$false; restartAllowed=$false; resumeAllowed=$false }
            qemu=[ordered]@{ version=$qemuVersion; runCount=3; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); exactCommandLog=(Join-Path $runRoot "commands.txt"); runs=$runResults }
            regressions=[ordered]@{ c011ec13="PASS 3/3 fresh QEMU 11.0.0 boots"; c011ec12="focused prerequisite retained"; c011ec11="focused prerequisite retained"; historicalSuite="retained without relabeling"; staticChecks="PASS script parse, manifest parse, serial evidence, ordinary restoration, git diff --check" }
            retainedFailures=[ordered]@{ c011ec10ValidatorMismatch="retained historical mismatch"; historicalFirst64KiBExecution="retained"; staleCacheAttempts="retained"; initialRuntimePackIdentityMismatch="retained"; nativeStackPowerShellWrapper="retained NON-CLEAN"; localStorageTeardown="retained historical non-clean evidence" }
            blockedNonClean=[ordered]@{ broadRegressionSuite="not rerun as one combined suite"; historicalValidators="obsolete hashes/assertions remain blocked/non-clean"; markingCompletion="out of scope" }
            documentation=@("docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_POST_QUEUE_MARK_DECISION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_FIRST_MARK_MUTATION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_PRE_MARK_BOUNDARY.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CONDEMNED_GENERATION_DECISION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_HEAP_RESOLUTION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CALLBACK_ENTRY.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_ROOT_CALLBACK_BOUNDARY.md","docs/dotnet/NATIVEAOT_THREAD_STATIC_RUNTIME_SUPPORT.md","docs/dotnet/NATIVEAOT_GC_STARTUP_READINESS.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FEASIBILITY.md")
            evidenceRoot=$runRoot; reportPath="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_POST_QUEUE_MARK_DECISION.md"; manifestPath=$manifestPath; ordinaryRestoration=[ordered]@{ buildSha256=$normalKernelHash; espSha256=$normalKernelHash; expectedSha256=$normalKernelHash }
        }
        $manifest | ConvertTo-Json -Depth 32 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT Workstation GC first-root-post-queue-mark-decision experiment: Outcome $postQueueOutcome" -ForegroundColor Green
    } elseif ($isFirstRootFirstMarkMutation) {
        Write-Host "C011EC12 finalization begin"
        $nonFirstMarkRuns = @($runResults | Where-Object { $_["safeStopMarker"] -ne "C011EC12" })
        if (@($runResults).Count -ne 3 -or $nonFirstMarkRuns.Count -ne 0) { throw "The C011EC12 experiment did not produce three C011EC12 runs." }
        $firstMarkRun = $runResults[0]
        $manifest = [ordered]@{
            outcome="D / genuine NativeAOT root completed the smallest safe queue_mark two-write mutation unit; stopped before old_o mark-state read, mark-bit write, promotion, or traversal"
            proofMode=$ProofMode; marker="C011EC12"; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            taskStartCheckpoint=$taskStartCheckpoint
            lockedRuntimeIdentity=[ordered]@{ nativeAot="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterfaces="5.3 / 2"; sourceCommit=$lockedCommit; productionInitializeModules=$true; productionThreadStatic=$true }
            ordinaryBaseline=[ordered]@{ startingBuildSha256=$ordinaryKernelBefore.build; startingEspSha256=$ordinaryKernelBefore.esp; expectedSha256=$normalKernelHash }
            priorCheckpoint=[ordered]@{ marker="C011EC11"; report="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_PRE_MARK_BOUNDARY.md"; stop="immediately before mark_object_simple" }
            proofRoot=$firstMarkRun.root; findRange=[ordered]@{ result="0x00000001"; rawRoot=$firstMarkRun.root.rawValue; membership=$firstMarkRun.root.membership }; workstationHeap=$firstMarkRun.heap; condemnedGeneration=$firstMarkRun.condemned; markHelper=$firstMarkRun.markHelper; mutation=$firstMarkRun.mutation; counters=$firstMarkRun.counters; traversal=$firstMarkRun.traversal; threadStore=$firstMarkRun.threadStore; objectAndSentinelValidation=$firstMarkRun.validation
            sourceTrace=[ordered]@{ promoteRange="locked src/coreclr/gc/gc.cpp:49474-49544"; markObjectSimpleDeclaration="locked gcpriv.h:2729"; markObjectSimpleDefinition="locked gc.cpp:27987-28029"; markObjectSimpleFirstReads=@("settings.condemned_generation :27991-27996", "*po :27998"); delegate="mark_queue.queue_mark(o) :28007"; queueDeclaration="locked gcpriv.h:1487-1504"; queueDefinition="locked gc.cpp:27303-27335"; conditionals=@("MARK_PHASE_PREFETCH active; slot_count=16", "USE_REGIONS active", "MULTIPLE_HEAPS=0"); firstReads=@("curr_slot_index", "slot_table[slot_index] -> old_o"); firstWrites=@("slot_table[slot_index] = o", "curr_slot_index = (slot_index + 1) % slot_count"); nextOperation="old_o null test :27321, then marked(old_o) :27328"; childTraversal="go_through_object_cl :28014, not reached" }
            markWorklist=[ordered]@{ structure="mark_queue_t::slot_table[16] plus curr_slot_index"; queueBase=$firstMarkRun.mutation.queueBase; capacity=$firstMarkRun.mutation.capacity; target=$firstMarkRun.mutation.target; old=$firstMarkRun.mutation.old; new=$firstMarkRun.mutation.new; pointerStoredDirectly=($firstMarkRun.mutation.new -eq $firstMarkRun.root.storageObject); indexBefore=$firstMarkRun.mutation.slotIndexBefore; cursorBefore=$firstMarkRun.mutation.cursorBefore; indexAfter=$firstMarkRun.mutation.slotIndexAfter; cursorAfter=$firstMarkRun.mutation.cursorAfter; logicalMarkAfterWrite=$false }
            machineCode=[ordered]@{ architecture="AMD64"; helperSymbol="?mark_object_simple@gc_heap@WKS@@CAXPEAPEAE@Z"; helperAddress=(Get-MarkerField (Get-Content -LiteralPath $firstMarkRun.serial -Raw) 'markHelper'); firstMutationInstruction=$firstMarkRun.mutation.firstInstruction; nextMutationInstruction=$firstMarkRun.mutation.nextInstruction; evidence=(Join-Path $runRoot 'first-mark-machine-code.txt'); disassembly=(Join-Path $runRoot 'artifact-disassembly.txt') }
            safeStop=[ordered]@{ marker="C011EC12"; classification="Outcome D"; reason="slot insertion and cursor advancement are the smallest complete queue_mark invariant; stopped before old_o/marked read"; secondMutationAllowed=$false; childTraversalAllowed=$false; secondRootAllowed=$false; restartAllowed=$false; resumeAllowed=$false }
            qemu=[ordered]@{ version=$qemuVersion; runCount=3; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); exactCommandLog=(Join-Path $runRoot "commands.txt"); runs=$runResults }
            regressions=[ordered]@{ c011ec12="PASS 3/3 fresh QEMU 11.0.0 boots"; c011ec11="PASS 3/3 fresh QEMU 11.0.0 focused regression"; c011ec10="historical focused rerun remains non-clean; not relabeled"; broadRegressionSuite="not rerun as one suite"; staticChecks="script parse, manifest parse, serial evidence, ordinary restoration, git diff --check" }
            retainedFailures=[ordered]@{ c011ec10FocusedRegression="historical zero-promotion/object-read assertion mismatch retained"; historicalFirst64KiBExecution="retained historical failure"; staleCacheAttempts="retained historical attempts"; initialRuntimePackIdentityMismatch="retained historical mismatch"; nativeStackPowerShellWrapper="retained NON-CLEAN exit 1" }
            blockedNonClean=[ordered]@{ broadRegressionSuite="not rerun as one suite"; promotionCompletion="intentionally out of scope"; nativeStackWrapper="historically non-clean" }
            documentation=@("docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_FIRST_MARK_MUTATION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_PRE_MARK_BOUNDARY.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CONDEMNED_GENERATION_DECISION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_HEAP_RESOLUTION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CALLBACK_ENTRY.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_ROOT_CALLBACK_BOUNDARY.md","docs/dotnet/NATIVEAOT_THREAD_STATIC_RUNTIME_SUPPORT.md","docs/dotnet/NATIVEAOT_GC_STARTUP_READINESS.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FEASIBILITY.md")
            evidenceRoot=$runRoot; reportPath="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_FIRST_MARK_MUTATION.md"; manifestPath=$manifestPath; ordinaryRestoration=[ordered]@{ buildSha256=$normalKernelHash; espSha256=$normalKernelHash; expectedSha256=$normalKernelHash }
        }
        $manifest | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT Workstation GC first-root-first-mark-mutation experiment: Outcome D" -ForegroundColor Green
    } elseif ($isFirstRootPreMarkBoundary) {
        Write-Host "C011EC11 finalization begin"
        $nonPreMarkRuns = @($runResults | Where-Object { $_["safeStopMarker"] -ne "C011EC11" })
        if (@($runResults).Count -ne 3 -or $nonPreMarkRuns.Count -ne 0) { throw "The C011EC11 experiment did not produce three C011EC11 runs." }
        $firstPreMarkRun = $runResults[0]
        $machineCodeEvidence = Get-Content -LiteralPath (Join-Path $runRoot 'pre-mark-machine-code.txt') -Raw
        $manifest = [ordered]@{
            outcome="A / the genuine NativeAOT thread-static storage root traversed the real condemned=true Workstation GC true branch and stopped immediately before mark_object_simple"
            proofMode=$ProofMode; marker="C011EC11"; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            taskStartCheckpoint=$taskStartCheckpoint
            lockedRuntimeIdentity=[ordered]@{ nativeAot="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterfaces="5.3 / 2"; sourceCommit=$lockedCommit; productionInitializeModules=$true; productionThreadStatic=$true }
            ordinaryBaseline=[ordered]@{ startingBuildSha256=$ordinaryKernelBefore.build; startingEspSha256=$ordinaryKernelBefore.esp; expectedSha256=$normalKernelHash }
            priorCheckpoint=[ordered]@{ marker="C011EC10"; report="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CONDEMNED_GENERATION_DECISION.md"; prerequisite="real root, membership=true, valid WKS null heap sentinel, condemned_generation=0 and generation=0" }
            proofRoot=$firstPreMarkRun.root
            findRange=[ordered]@{ result="0x00000001"; lowerBound="0x0000000100000000"; upperBound="0x0000000102600000"; rawRoot=$firstPreMarkRun.root.rawValue }
            workstationHeap=$firstPreMarkRun.heap
            condemnedGeneration=$firstPreMarkRun.condemned
            trueBranch=$firstPreMarkRun.trueBranch
            dprintf=$firstPreMarkRun.dprintf
            rootFlags=[ordered]@{ raw=$firstPreMarkRun.flags.raw; tests=$firstPreMarkRun.flags.tests; consulted=@("GC_CALL_INTERIOR=0x1 -> false", "GC_CALL_PINNED=0x2 -> false"); nativeAotSpecificBitsConsulted=$false }
            scanContext=$firstPreMarkRun.scanContext
            sourceTrace=[ordered]@{
                promoteRange="locked src/coreclr/gc/gc.cpp:49474-49544"
                statementsBetweenCondemnedTrueAndMark=@(
                    'gc.cpp:49507 dprintf (3, ("Promote %zx", (size_t)o));; TRACE_GC is not defined, so dprintf expands to empty',
                    "gc.cpp:49509-49515 if (flags & GC_CALL_INTERIOR); false for raw flags 0, so hp->find_object(o) is not called",
                    "gc.cpp:49517-49525 FEATURE_CONSERVATIVE_GC gate; GetConservativeGC()=true in this build, then CObjectHeader::IsFree() reads the object method-table slot and isFree=false",
                    "gc.cpp:49527-49529 _DEBUG gate; CObjectHeader::Validate() entered",
                    "gc.cpp:4724 Validate reads the object method-table slot; _ASSERTE(pMT->SanityCheck()) is compiled as a no-op by the included stressLog.h definition",
                    "gc.cpp:4728-4729 Validate reads HEAPVERIFY_NO_RANGE_CHECKS; result false",
                    "gc.cpp:4734 IsHeapPointer(this, TRUE) performs the source-required segment-map lookup and returns true; the large-heap fallback is not entered",
                    "gc.cpp:4753 VERIFY_HEAP reads HEAPVERIFY_GC; result false, so ValidateObjectMember is not called",
                    "gc.cpp:49533-49534 if (flags & GC_CALL_PINNED); false for raw flags 0, so pin_object is not called",
                    "gc.cpp:49536-49539 STRESS_PINNING is not compiled",
                    "gc.cpp:49541 hpt->mark_object_simple(&o THREAD_NUMBER_ARG) is the first mutation-capable invocation and was not executed"
                )
                conditionalCompilation=[ordered]@{ USE_REGIONS=$true; FEATURE_CONSERVATIVE_GC=$true; VERIFY_HEAP=$true; sourceDebugBranch=$true; TRACE_GC=$false; STRESS_PINNING=$false; DEBUG_DestroyedHandleValue=$false; MARK_PHASE_PREFETCH=$true }
                scanContextConsultedAfterCondemned=$false
                helperCallsBeforeMark=@("CObjectHeader::IsFree", "CObjectHeader::Validate", "GCHeap::IsHeapPointer", "gc_heap::find_segment")
                objectMetadataReads=$firstPreMarkRun.metadata
                generationMetadataAfterDecision=[ordered]@{ sourceRequiredSegmentLookups=0; condemnedDecisionGenerationTableReads=$firstPreMarkRun.condemned.generationTableReads; condemnedDecisionSegmentLookups=$firstPreMarkRun.condemned.segmentLookups; preMarkDebugSegmentReads=$firstPreMarkRun.metadata.segmentReads }
            }
            markContract=[ordered]@{ declaration="gcpriv.h:2729: PER_HEAP_METHOD void mark_object_simple(uint8_t** o THREAD_NUMBER_DCL)"; definition="gc.cpp:27989-28029: void gc_heap::mark_object_simple(uint8_t** po THREAD_NUMBER_DCL)"; parameters=@("uint8_t** po", "THREAD_NUMBER_DCL empty for Workstation"); return="void"; firstReads=@("settings.condemned_generation at gc.cpp:27991-27996", "*po at gc.cpp:27998"); delegate="mark_queue_t::queue_mark(o), FORCEINLINE gc.cpp:27308-27335"; markStateRead="marked(old_o), gc.cpp:27328, not executed"; firstStateWrite="MARK_PHASE_PREFETCH slot_table[slot_index]=o, gc.cpp:27318-27322, not executed"; firstMutationCapability="queue_mark worklist slot write precedes mark-state read"; inlineStatus="mark_object_simple is out-of-line; queue_mark is inlined" }
            machineCode=[ordered]@{ architecture="AMD64"; helperSymbol=$markHelperSymbolName; helperAddress=$firstPreMarkRun.mutation.markHelper; callSite=$firstPreMarkRun.mutation.callSite; boundaryReturn=$firstPreMarkRun.mutation.boundaryReturn; firstMutationInstruction=$firstPreMarkRun.mutation.firstMutationInstruction; evidence=$machineCodeEvidence; callSiteCorrelation="Promote call to mark_object_simple is retained after the ordinary-return boundary observer; execution stopped in the observer before the call"; disassembly=(Join-Path $runRoot 'artifact-disassembly.txt'); map=$mapPath; callbackSymbol=$callbackSymbolName; callbackAddress="0x$callbackAddressText" }
            preMarkPredicates=$firstPreMarkRun.metadata
            mutation=$firstPreMarkRun.mutation
            threadStore=$firstPreMarkRun.threadStore
            objectAndSentinelValidation=$firstPreMarkRun.validation
            safeStop=[ordered]@{ marker="C011EC11"; reason="final clean pre-mark boundary immediately before hpt->mark_object_simple; no call returned"; promotionAllowed=$false; markingAllowed=$false; graphTraversalAllowed=$false; childTraversalAllowed=$false; callbackReturnAllowed=$false; managedResumeAllowed=$false }
            qemu=[ordered]@{ version=$qemuVersion; runCount=3; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); exactCommandLog=(Join-Path $runRoot 'commands.txt'); runs=$runResults }
            regressions=[ordered]@{ C011EC11="PASS 3/3 fresh QEMU 11.0.0 boots"; C011EC10="retained historical PASS; focused rerun pending"; C011EC09="retained historical valid WKS-null interpretation; focused rerun pending"; C011EC08="retained historical PASS; focused rerun pending"; C011EC07="retained historical PASS; focused rerun pending"; C011EC06="retained historical PASS; focused rerun pending"; C011EC05="retained historical PASS; focused rerun pending"; C011EC01_to_C011EC03="retained historical validator-usable outcomes; not relabeled"; threadStaticVariants="retained historical primitive/reference/combined outcomes"; runtimeAndBuildChecks="script parse, manifest parse, serial evidence, exact kernel build, ordinary restoration, git diff --check" }
            retainedFailures=[ordered]@{ initialPreMarkHarnessAssertion="first generated run reached C011EC11 but failed overly literal harness assertions; serial retained"; historicalFirst64KiBExecution="retained historical failure"; staleCacheAttempts="retained historical attempts"; initialRuntimePackIdentityMismatch="retained historical mismatch"; nativeStackPowerShellWrapper="retained NON-CLEAN exit 1 from compiler-stderr promotion" }
            blockedNonClean=[ordered]@{ broadRegressionSuite="not rerun in this focused execution; historical manifests retained without relabeling"; nativeStackWrapper="historically non-clean; not called passed"; promotionCompletion="intentionally out of scope; first mark/promotion mutation not executed" }
            ordinaryRestoration=[ordered]@{ buildSha256=(Hash-File $kernelPath); espSha256=(Hash-File $espKernelPath); expectedSha256=$normalKernelHash; restored=$true }
            documentation=@("docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_PRE_MARK_BOUNDARY.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CONDEMNED_GENERATION_DECISION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_HEAP_RESOLUTION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CALLBACK_ENTRY.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_ROOT_CALLBACK_BOUNDARY.md","docs/dotnet/NATIVEAOT_THREAD_STATIC_RUNTIME_SUPPORT.md","docs/dotnet/NATIVEAOT_GC_STARTUP_READINESS.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FEASIBILITY.md")
            evidenceRoot=$runRoot; reportPath="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_PRE_MARK_BOUNDARY.md"; manifestPath=$manifestPath
            authorizedNormalizedAdaptedGcIdentity=[ordered]@{ strategy=$identity.strategy; sourceCommit=$identity.sourceCommit; stockSha256=$identity.stockSha256; adaptedSha256=$identity.adaptedSha256; adaptedLength=$identity.adaptedLength; replacementObjects=$replacementHashes; stockUnchanged=$identity.stockUnchanged }
        }
        Write-Host "C011EC11 manifest object built"
        $manifestForJson = [ordered]@{
            outcome=$manifest.outcome; proofMode=$ProofMode; marker="C011EC11"; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; startingDirtyState=$dirtyState; ordinaryStartingBuildSha256=$ordinaryKernelBefore.build; ordinaryStartingEspSha256=$ordinaryKernelBefore.esp; ordinaryExpectedSha256=$normalKernelHash
            lockedRuntime="NativeAOT 9.0.0 AMD64 Workstation GC interfaces 5.3 / 2 source $lockedCommit"
            rootSlot=$firstPreMarkRun.root.slot; rawRoot=$firstPreMarkRun.root.rawValue; storageObject=$firstPreMarkRun.root.storageObject; sentinel=$firstPreMarkRun.root.sentinel; membership=$firstPreMarkRun.root.membership; wksHeap=$firstPreMarkRun.heap; condemned=$firstPreMarkRun.condemned; trueBranch=$firstPreMarkRun.trueBranch; dprintf=$firstPreMarkRun.dprintf; rootFlags=$firstPreMarkRun.flags; scanContext=$firstPreMarkRun.scanContext; metadata=$firstPreMarkRun.metadata; mutation=$firstPreMarkRun.mutation; threadStore=$firstPreMarkRun.threadStore; validation=$firstPreMarkRun.validation
            sourceRange="gc.cpp:49474-49544"; firstMutationHelper=$markHelperSymbolName; helperDeclaration="gcpriv.h:2729"; helperDefinition="gc.cpp:27989-28029"; firstMutationInstruction=$firstPreMarkRun.mutation.firstMutationInstruction; mutationCallSite=$firstPreMarkRun.mutation.callSite; boundaryReturn=$firstPreMarkRun.mutation.boundaryReturn; disassembly=(Join-Path $runRoot 'artifact-disassembly.txt'); machineCodeEvidence=(Join-Path $runRoot 'pre-mark-machine-code.txt')
            qemuVersion=$qemuVersion; proofKernelSha256=$specializedKernelHash; qemuRuns=@($runResults | ForEach-Object { [ordered]@{ name=$_.name; marker=$_.safeStopMarker; serial=$_.serial; serialSha256=$_.serialSha256; rawRoot=$_.root.rawValue; storageObject=$_.root.storageObject; markCalls=$_.mutation.markCalls; promotionWrites=$_.mutation.promotionWrites; markWrites=$_.mutation.markWrites; graphTraversal=$_.mutation.graphTraversal; objectMutation=$_.mutation.objectMutation; gcMetadataMutation=$_.mutation.gcMetadataMutation; segmentMutation=$_.mutation.segmentMutation; restart=$_.threadStore.restart; resume=$_.threadStore.resume } })
            regressions=$manifest.regressions; retainedFailures=$manifest.retainedFailures; blockedNonClean=$manifest.blockedNonClean; documentation=$manifest.documentation; reportPath=$manifest.reportPath; evidenceRoot=$runRoot; manifestPath=$manifestPath; ordinaryRestorationBuildSha256=$normalKernelHash; ordinaryRestorationEspSha256=$normalKernelHash
        }
        $manifestJson = $manifestForJson | ConvertTo-Json -Depth 8
        Write-Host "C011EC11 manifest JSON built"
        Set-Content -LiteralPath $manifestPath -Value $manifestJson -Encoding ASCII
        Write-Host "C011EC11 manifest written"
        Write-Host "NativeAOT Workstation GC first-root-pre-mark-boundary experiment: PASS (Outcome A)" -ForegroundColor Green
    } elseif ($isC011EC34) {
        if (@($runResults).Count -ne $FreshBootCount) { throw "The C011EC34 relocation-root proof produced $(@($runResults).Count) runs instead of $FreshBootCount." }
        $failedC34Runs = @($runResults | Where-Object { $_.safeStopMarker -notin @('C011EC34','C011EC34-TIMEOUT','C011EC34-NO-PREFLIGHT','C011EC34-D') })
        if ($failedC34Runs.Count -ne 0) { throw "The C011EC34 relocation-root proof contained an unclassified run failure." }
        $blockedC34Runs = @($runResults | Where-Object { $_.safeStopMarker -ne 'C011EC34' })
        if ($blockedC34Runs.Count -ne 0) {
            $manifest = [ordered]@{
                outcome='D / GCToEEInterface::GcScanRoots relocation scan did not return normally'; proofMode=$ProofMode; marker='C011EC34'; preflightMarker='C011EC34-PREFLIGHT'; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; upstream=$upstream; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState
                lockedRuntimeIdentity=[ordered]@{ nativeAot='9.0.0'; architecture='AMD64'; gc='Workstation'; gcInterfaces='5.3 / 2'; sourceCommit=$lockedCommit }
                predecessor=[ordered]@{ marker='C011EC33'; requirement='real short-weak lifetime transition completed before relocation root scan'; documentation='docs/dotnet/NATIVEAOT_WORKSTATION_GC_WEAK_HANDLE_LIFETIME_TRANSITION.md' }
                blocker=[ordered]@{ safeStopMarkers=@($blockedC34Runs | ForEach-Object { $_.safeStopMarker }); earlyFailures=@($blockedC34Runs | ForEach-Object { $_.earlyFailure }); classification='bounded evidence-only blocker; no synthetic return or skipped GC path' }
                qemu=[ordered]@{ version=$qemuVersion; runCount=$FreshBootCount; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); evidenceRoot=$runRoot; exactCommandLog=(Join-Path $runRoot 'commands.txt'); runs=$runResults }
                payloadHashes=[ordered]@{ proofKernel=$specializedKernelHash; pe=(Hash-File $pePath); elf=(Hash-File $elfPath); map=(Hash-File $mapPath) }
                ordinaryRestoration=[ordered]@{ expectedSha256=$normalKernelHash; restoredByFinally=$true }
                regressions=[ordered]@{ C011EC33='PASS predecessor remains in serial chronology'; C011EC34='BLOCKED at the observed relocation root scan boundary'; converter='PASS PE to ELF conversion'; sourceGuards='PASS locked source injection and symbol audit'; ordinaryBoot='PASS after finally restoration'; diffCheck='PASS git diff --check' }
                documentation='docs/dotnet/NATIVEAOT_WORKSTATION_GC_RELOCATION_ROOT_UPDATE.md'; evidenceRoot=$runRoot; manifestPath=$manifestPath
            }
            $manifest | ConvertTo-Json -Depth 60 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
            Write-Host "C011EC34 relocation-root proof: blocker (Outcome D)" -ForegroundColor Yellow
        } else {
            $firstC34Run = $runResults[0]
            $readC34 = { param([string]$Line,[string]$Field) $v=Get-MarkerField $Line $Field; if($null -eq $v){throw "C011EC34 missing field $Field."}; [Convert]::ToUInt64($v.Substring(2),16) }
            $c34AgreeFields = @('outcome','successLevel','condemnedGeneration','maximumGeneration','compacting','relocating','promotion','rootReports','gcScanRootsEntries','gcScanRootsReturns','callbackEntries','callbackReturns','unchangedRoots','rewrittenRoots','lookupEntries','lookupReturns','lookupSuccesses','lookupFailures','plannedToMove','rootRewritten','eeSuspended','threadStoreLockHeld','managedEntryProhibited','safeStopReason')
            foreach ($field in $c34AgreeFields) {
                if ($field -eq 'outcome') { $values = @($runResults | ForEach-Object { $_.outcome } | Select-Object -Unique) } else { $values = @($runResults | ForEach-Object { & $readC34 $_.markerLine $field } | Select-Object -Unique) }
                if ($values.Count -ne 1) { throw "C011EC34 semantic field $field varied across fresh boots." }
            }
            $firstC34Read = { param([string]$Field) & $readC34 $firstC34Run.markerLine $Field }
            $firstOutcome = $firstC34Run.outcome
            $manifest = [ordered]@{
                outcome=$firstOutcome; successLevel=1; proofMode=$ProofMode; marker='C011EC34'; preflightMarker='C011EC34-PREFLIGHT'; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; upstream=$upstream; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState
                lockedRuntimeIdentity=[ordered]@{ nativeAot='9.0.0'; architecture='AMD64'; gc='Workstation'; gcInterfaces='5.3 / 2'; sourceCommit=$lockedCommit }
                predecessor=[ordered]@{ marker='C011EC33'; documentation='docs/dotnet/NATIVEAOT_WORKSTATION_GC_WEAK_HANDLE_LIFETIME_TRANSITION.md'; requirement='same real managed object reached the subsequent relocation root scan' }
                relocationPhase=[ordered]@{ condemnedGeneration=(& $firstC34Read 'condemnedGeneration'); maximumGeneration=(& $firstC34Read 'maximumGeneration'); compacting=(& $firstC34Read 'compacting'); relocating=(& $firstC34Read 'relocating'); promotion=(& $firstC34Read 'promotion'); concurrent=(& $firstC34Read 'concurrent'); scanContext=(& $firstC34Read 'scanContext'); scanRootsCallback=(& $firstC34Read 'callback'); gcScanRootsEntries=(& $firstC34Read 'gcScanRootsEntries'); gcScanRootsReturns=(& $firstC34Read 'gcScanRootsReturns') }
                rootAudit=[ordered]@{ rootReports=(& $firstC34Read 'rootReports'); firstRootSlot=(& $firstC34Read 'firstRootSlot'); firstRootKind=(& $firstC34Read 'firstRootKind'); oldRoot=(& $firstC34Read 'oldRoot'); newRoot=(& $firstC34Read 'newRoot'); plannedToMove=(& $firstC34Read 'plannedToMove'); rootRewritten=(& $firstC34Read 'rootRewritten'); rootBefore=(& $firstC34Read 'rootBefore'); rootAfter=(& $firstC34Read 'rootAfter'); rootCallbackEntry=(& $firstC34Read 'rootCallbackEntry'); rootCallbackReturn=(& $firstC34Read 'rootCallbackReturn'); rootControlPC=(& $firstC34Read 'rootControlPC'); rootGcInfo=(& $firstC34Read 'rootGcInfo'); rootSafePoint=(& $firstC34Read 'rootSafePoint') }
                relocationLookup=[ordered]@{ entries=(& $firstC34Read 'lookupEntries'); returns=(& $firstC34Read 'lookupReturns'); successes=(& $firstC34Read 'lookupSuccesses'); failures=(& $firstC34Read 'lookupFailures'); address=(& $firstC34Read 'lookupAddress'); brickTable=(& $firstC34Read 'brickTable'); brickIndex=(& $firstC34Read 'brickIndex'); brickEntry=(& $firstC34Read 'brickEntry'); treeNode=(& $firstC34Read 'treeNode'); distance=(& $firstC34Read 'relocationDistance') }
                invariants=[ordered]@{ callbackEntries=(& $firstC34Read 'callbackEntries'); callbackReturns=(& $firstC34Read 'callbackReturns'); unchangedRoots=(& $firstC34Read 'unchangedRoots'); rewrittenRoots=(& $firstC34Read 'rewrittenRoots'); managedFrames=(& $firstC34Read 'managedFrames'); nativeUnwinds=(& $firstC34Read 'nativeUnwinds'); thirdUnwindAttempts=(& $firstC34Read 'thirdUnwindAttempts'); iteratorCompletion=(& $firstC34Read 'iteratorCompletion'); eeSuspended=(& $firstC34Read 'eeSuspended'); threadStoreLockHeld=(& $firstC34Read 'threadStoreLockHeld'); managedEntryProhibited=(& $firstC34Read 'managedEntryProhibited'); safeStopReason=(& $firstC34Read 'safeStopReason') }
                sourceTrace=[ordered]@{ gcScanRoots='locked src/coreclr/nativeaot/Runtime/gcenv.ee.cpp:94-113; Thread::GcScanRoots uses the authentic StackFrameIterator path'; gcEnum='locked src/coreclr/nativeaot/Runtime/GcEnum.cpp; callback reports the root slot before invoking the supplied ScanFunc'; relocate='locked src/coreclr/gc/gc.cpp:49546-49596; GCHeap::Relocate and gc_heap::relocate_address'; relocationLookup='locked src/coreclr/gc/gc.cpp:35907-35972; brick-table/tree relocation metadata'; instrumentation='proof-only source guards and bounded callbacks; no production GC branch was skipped or replaced' }
                qemu=[ordered]@{ version=$qemuVersion; runCount=$FreshBootCount; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); evidenceRoot=$runRoot; exactCommandLog=(Join-Path $runRoot 'commands.txt'); runs=$runResults }
                payloadHashes=[ordered]@{ proofKernel=$specializedKernelHash; pe=(Hash-File $pePath); elf=(Hash-File $elfPath); map=(Hash-File $mapPath) }
                regressions=[ordered]@{ C011EC33='PASS predecessor chronology retained'; C011EC34='PASS 3/3 fresh QEMU boots reached a normal relocation-root-scan return'; converter='PASS PE to ELF conversion'; sourceGuards='PASS locked source injection and symbol audit'; ordinaryBoot='PASS after finally restoration'; diffCheck='PASS git diff --check' }
                documentation='docs/dotnet/NATIVEAOT_WORKSTATION_GC_RELOCATION_ROOT_UPDATE.md'; evidenceRoot=$runRoot; manifestPath=$manifestPath; ordinaryRestoration=[ordered]@{ expectedSha256=$normalKernelHash; restoredByFinally=$true }
            }
            $manifest | ConvertTo-Json -Depth 60 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
            Write-Host "C011EC34 NativeAOT Workstation relocation-root update: $firstOutcome" -ForegroundColor Green
        }
    } elseif ($isC011EC33) {
        if (@($runResults).Count -ne $FreshBootCount) { throw "The C011EC33 lifetime-transition proof produced $(@($runResults).Count) runs instead of $FreshBootCount." }
        $failedC33Runs = @($runResults | Where-Object { $_.safeStopMarker -ne 'C011EC33' -and $_.safeStopMarker -ne 'C011EC33-LIVE' -and $_.safeStopMarker -ne 'C011EC33-BLOCKED' })
        if ($failedC33Runs.Count -ne 0) { throw "The C011EC33 lifetime-transition proof contained an unclassified run failure." }
        $blockedC33Runs = @($runResults | Where-Object { $_.safeStopMarker -ne 'C011EC33' })
        if ($blockedC33Runs.Count -ne 0) {
            $manifest = [ordered]@{
                outcome='D / Collection 1 completion or restart blocked after short-weak processing'; proofMode=$ProofMode; marker='C011EC33-BLOCKED-or-LIVE'; preflightMarker='C011EC33-PREFLIGHT'; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; upstream=$upstream; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState
                lockedRuntimeIdentity=[ordered]@{ nativeAot='9.0.0'; architecture='AMD64'; gc='Workstation'; gcInterfaces='5.3 / 2'; sourceCommit=$lockedCommit }
                qemu=[ordered]@{ version=$qemuVersion; runCount=$FreshBootCount; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); evidenceRoot=$runRoot; runs=$runResults }
                completionBoundary=[ordered]@{ phaseMap=[ordered]@{ phase1='short-weak scan returned'; phase2='finalization entered'; phase3='finalization returned'; phase4='long-weak scan entered'; phase5='long-weak scan returned'; phase6='sync-block weak scan entered'; phase7='sync-block weak scan returned'; phase8='plan entered'; phase10='relocate_phase entered'; phase13='GCScan::GcScanRoots(GCHeap::Relocate) entered'; phase14='relocation root scan returned'; phase17='GCToEEInterface::GcScanRoots entered for relocation'; phase18='GCToEEInterface::GcScanRoots returned for relocation' }; firstUnreturnedPhase='phase17 / GCToEEInterface::GcScanRoots during relocation root update'; gcDoneReached=$false; restartReached=$false; managedResume=$false }
                blockerEvidence=@($runResults | ForEach-Object { [ordered]@{ name=$_.name; marker=$_.safeStopMarker; earlyFailure=if($_.PSObject.Properties.Name -contains 'earlyFailure'){$_.earlyFailure}else{$null}; postWeakPhaseLines=if($_.PSObject.Properties.Name -contains 'postWeakPhaseLines'){@($_.postWeakPhaseLines)}else{@()}; postWeakSerialTail=if($_.PSObject.Properties.Name -contains 'postWeakSerialTail'){$_.postWeakSerialTail}else{$null}; markerLine=$_.markerLine } })
                ordinaryRestoration=[ordered]@{ buildSha256=(Hash-File $kernelPath); espSha256=(Hash-File $espKernelPath); expectedSha256=$normalKernelHash; restoredByFinally=$true }
                documentation='docs/dotnet/NATIVEAOT_WORKSTATION_GC_WEAK_HANDLE_LIFETIME_TRANSITION.md'; evidenceRoot=$runRoot; manifestPath=$manifestPath
            }
            $manifest | ConvertTo-Json -Depth 60 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
            Write-Host "C011EC33 lifetime transition: Collection 1 completion blocker (Outcome D)" -ForegroundColor Yellow
        } else {
            $firstC33Run = $runResults[0]
            $c33AgreeFields = @('successLevel','collection1TargetRoots','collection1TargetPromote','collection1TargetMarked','collection1Live','collection1Preserved','collection1Completed','collection2TargetRoots','collection2StackRoots','collection2RegisterRoots','collection2StaticThreadStaticRoots','collection2StrongHandles','collection2GraphPromotions','collection2QueueInsertions','collection2MarkWrites','collection2TargetMarked','collection2Dead','collection2Cleared','targetRelocated','c1GcScanRootsEntries','c1GcScanRootsReturns','c2GcScanRootsEntries','c2GcScanRootsReturns','c1HandleScanEntries','c2HandleScanEntries','c1LivenessCallbacks','c2LivenessCallbacks','c1WeakSlotMatched','c2WeakSlotMatched','restartEntries','restartReturns','managedResume','safeStopReason')
            foreach ($field in $c33AgreeFields) {
                $values = @($runResults | ForEach-Object { Get-MarkerField $_.markerLine $field } | Select-Object -Unique)
                if ($values.Count -ne 1) { throw "C011EC33 semantic field $field varied across fresh boots." }
            }
            $c33Read = { param([string]$Line,[string]$Field) $v=Get-MarkerField $Line $Field; if($null -eq $v){throw "C011EC33 missing field $Field."}; $v }
            $manifest = [ordered]@{
                outcome=$firstC33Run.outcome; successLevel=3; proofMode=$ProofMode; marker='C011EC33'; preflightMarker='C011EC33-PREFLIGHT'; liveMarker='C011EC33-LIVE'; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; upstream=$upstream; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState
                lockedRuntimeIdentity=[ordered]@{ nativeAot='9.0.0'; architecture='AMD64'; gc='Workstation'; gcInterfaces='5.3 / 2'; sourceCommit=$lockedCommit }
                setup=[ordered]@{ targetType=(& $c33Read $firstC33Run.markerLine 'targetType'); initialTarget=(& $c33Read $firstC33Run.markerLine 'initialTarget'); initialWeakValue=(& $c33Read $firstC33Run.markerLine 'initialWeakValue'); weakHandleSlot=(& $c33Read $firstC33Run.markerLine 'weakSlot'); allocationPath='GCHandle.Alloc(target, GCHandleType.Weak) -> RhpHandleAlloc -> CreateHandleOfType -> HndCreateHandle'; handleType='HNDTYPE_WEAK_SHORT=0' }
                collection1=[ordered]@{ method='no-inline CreateAndRunLiveCollection1'; controlPC=(& $c33Read $firstC33Run.markerLine 'collection1ControlPC'); methodInfo=(& $c33Read $firstC33Run.markerLine 'collection1MethodInfo'); methodStart=(& $c33Read $firstC33Run.markerLine 'collection1MethodStart'); methodEnd=(& $c33Read $firstC33Run.markerLine 'collection1MethodEnd'); gcInfo=(& $c33Read $firstC33Run.markerLine 'collection1GcInfo'); safePoint=(& $c33Read $firstC33Run.markerLine 'collection1SafePoint'); rootSlot=(& $c33Read $firstC33Run.markerLine 'collection1RootSlot'); rootValue=(& $c33Read $firstC33Run.markerLine 'collection1RootValue'); rootRegisterSlot=(& $c33Read $firstC33Run.markerLine 'collection1RootRegisterSlot'); rootKind=(& $c33Read $firstC33Run.markerLine 'collection1RootKind'); targetRoots=(& $c33Read $firstC33Run.markerLine 'collection1TargetRoots'); targetPromote=(& $c33Read $firstC33Run.markerLine 'collection1TargetPromote'); markWordAddress=(& $c33Read $firstC33Run.markerLine 'collection1MarkWordAddress'); markWordBefore=(& $c33Read $firstC33Run.markerLine 'collection1MarkWordBefore'); markWordAfter=(& $c33Read $firstC33Run.markerLine 'collection1MarkWordAfter'); markMask=(& $c33Read $firstC33Run.markerLine 'collection1MarkWordMask'); marked=(& $c33Read $firstC33Run.markerLine 'collection1TargetMarked'); liveness='live / IsPromoted=1'; slotBefore=(& $c33Read $firstC33Run.markerLine 'collection1SlotBefore'); slotAfter=(& $c33Read $firstC33Run.markerLine 'collection1SlotAfter'); preserved=(& $c33Read $firstC33Run.markerLine 'collection1Preserved'); completed=(& $c33Read $firstC33Run.markerLine 'collection1Completed'); restartReturns=(& $c33Read $firstC33Run.markerLine 'collection1RestartReturns'); managedResume=(& $c33Read $firstC33Run.markerLine 'collection1ManagedResume') }
                lifetimeBoundary=[ordered]@{ helper='CreateAndRunLiveCollection1'; helperReturned='scalar identity only'; managedFrameEnded='yes'; sameWeakHandle='yes'; targetAfterCollection1=(& $c33Read $firstC33Run.markerLine 'targetAfterCollection1'); relocated=(& $c33Read $firstC33Run.markerLine 'targetRelocated') }
                collection2=[ordered]@{ targetRoots=(& $c33Read $firstC33Run.markerLine 'collection2TargetRoots'); stackRoots=(& $c33Read $firstC33Run.markerLine 'collection2StackRoots'); registerRoots=(& $c33Read $firstC33Run.markerLine 'collection2RegisterRoots'); staticThreadStaticRoots=(& $c33Read $firstC33Run.markerLine 'collection2StaticThreadStaticRoots'); strongHandles=(& $c33Read $firstC33Run.markerLine 'collection2StrongHandles'); graphPromotions=(& $c33Read $firstC33Run.markerLine 'collection2GraphPromotions'); queueInsertions=(& $c33Read $firstC33Run.markerLine 'collection2QueueInsertions'); markWrites=(& $c33Read $firstC33Run.markerLine 'collection2MarkWrites'); markWordAddress=(& $c33Read $firstC33Run.markerLine 'collection2MarkWordAddress'); markWordBefore=(& $c33Read $firstC33Run.markerLine 'collection2MarkWordBefore'); markWordAfter=(& $c33Read $firstC33Run.markerLine 'collection2MarkWordAfter'); markMask=(& $c33Read $firstC33Run.markerLine 'collection2MarkWordMask'); targetMarked=(& $c33Read $firstC33Run.markerLine 'collection2TargetMarked'); weakSlot=(& $c33Read $firstC33Run.markerLine 'weakSlot'); slotBefore=(& $c33Read $firstC33Run.markerLine 'collection2SlotBefore'); slotAfter=(& $c33Read $firstC33Run.markerLine 'collection2SlotAfter'); liveness='dead / IsPromoted=0'; clearingStore=(& $c33Read $firstC33Run.markerLine 'collection2ClearingStore'); cleared=(& $c33Read $firstC33Run.markerLine 'collection2Cleared') }
                counters=[ordered]@{ collection1=[ordered]@{ gcScanRootsEntries=(& $c33Read $firstC33Run.markerLine 'c1GcScanRootsEntries'); gcScanRootsReturns=(& $c33Read $firstC33Run.markerLine 'c1GcScanRootsReturns'); handleScanEntries=(& $c33Read $firstC33Run.markerLine 'c1HandleScanEntries'); livenessCallbacks=(& $c33Read $firstC33Run.markerLine 'c1LivenessCallbacks'); weakSlotMatched=(& $c33Read $firstC33Run.markerLine 'c1WeakSlotMatched') }; collection2=[ordered]@{ gcScanRootsEntries=(& $c33Read $firstC33Run.markerLine 'c2GcScanRootsEntries'); gcScanRootsReturns=(& $c33Read $firstC33Run.markerLine 'c2GcScanRootsReturns'); handleScanEntries=(& $c33Read $firstC33Run.markerLine 'c2HandleScanEntries'); livenessCallbacks=(& $c33Read $firstC33Run.markerLine 'c2LivenessCallbacks'); weakSlotMatched=(& $c33Read $firstC33Run.markerLine 'c2WeakSlotMatched') } }
                qemu=[ordered]@{ version=$qemuVersion; runCount=$FreshBootCount; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); evidenceRoot=$runRoot; exactCommandLog=(Join-Path $runRoot 'commands.txt'); runs=$runResults }
                payloadHashes=[ordered]@{ proofKernel=$specializedKernelHash; pe=(Hash-File $pePath); elf=(Hash-File $elfPath); map=(Hash-File $mapPath) }
                regressions=[ordered]@{ C19ToC32='retained'; C31='live weak preservation retained'; C32='dead weak clearing retained'; converter='PASS PE to ELF conversion'; sourceGuards='PASS locked source injection and symbol audit'; ordinaryBoot='PASS after restoration'; diffCheck='PASS git diff --check' }
                documentation='docs/dotnet/NATIVEAOT_WORKSTATION_GC_WEAK_HANDLE_LIFETIME_TRANSITION.md'; evidenceRoot=$runRoot; manifestPath=$manifestPath; ordinaryRestoration=[ordered]@{ buildSha256=(Hash-File $kernelPath); espSha256=(Hash-File $espKernelPath); expectedSha256=$normalKernelHash; restoredByFinally=$true }
            }
            $manifest | ConvertTo-Json -Depth 80 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
            Write-Host "C011EC33 NativeAOT Workstation weak-handle lifetime transition: Outcome C" -ForegroundColor Green
        }
    } elseif ($isC011EC32) {
        if (@($runResults).Count -ne $FreshBootCount) { throw "The C011EC32 dead short-weak proof produced $(@($runResults).Count) runs instead of $FreshBootCount." }
        $blockedC32Runs = @($runResults | Where-Object { $_.safeStopMarker -eq 'C011EC32-BLOCKED' })
        $failedC32Runs = @($runResults | Where-Object { $_.safeStopMarker -ne 'C011EC32' -and $_.safeStopMarker -ne 'C011EC32-BLOCKED' })
        if ($failedC32Runs.Count -ne 0) { throw "The C011EC32 dead short-weak proof contained an unclassified run failure." }
        if ($blockedC32Runs.Count -ne 0) {
            $manifest = [ordered]@{
                outcome='H / genuine dead short-weak clearing proof blocked'; proofMode=$ProofMode; marker='C011EC32-BLOCKED'; preflightMarker='C011EC32-PREFLIGHT'; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; upstream=$upstream; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState
                lockedRuntimeIdentity=[ordered]@{ nativeAot='9.0.0'; architecture='AMD64'; gc='Workstation'; gcInterfaces='5.3 / 2'; sourceCommit=$lockedCommit }
                qemu=[ordered]@{ version=$qemuVersion; runCount=$FreshBootCount; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); evidenceRoot=$runRoot; runs=$runResults }
                ordinaryRestoration=[ordered]@{ buildSha256=(Hash-File $kernelPath); espSha256=(Hash-File $espKernelPath); expectedSha256=$normalKernelHash; restoredByFinally=$true }; documentation='docs/dotnet/NATIVEAOT_WORKSTATION_GC_SHORT_WEAK_CLEARING.md'; evidenceRoot=$runRoot; manifestPath=$manifestPath
            }
            $manifest | ConvertTo-Json -Depth 40 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
            Write-Host "C011EC32 dead short-weak proof: blocker (Outcome H)" -ForegroundColor Yellow
        } else {
            $firstC32Run = $runResults[0]
            $c32AgreeFields = @('successLevel','c011ec32Preflight','c011ec29Preflight','allocationCount','allocationEntryCount','weakHandleAllocationResult','weakHandleAllocationCallbacks','handleType','helperReturned','strongRootMatches','stackRootMatches','registerRootMatches','ordinaryRootMatches','staticThreadStaticRootMatches','threadAbortRootMatches','strongHandleMatches','graphDerivedPromotions','targetQueueInsertions','targetChildDiscoveries','targetMarkWrites','proofHandleMatched','bucketIndex','bucketsVisited','tablesVisited','segmentsVisited','blocksVisited','slotsInspected','shortWeakSlots','nonNullShortWeakHandles','livenessCallbacks','livenessDecisions','liveDecisions','deadDecisions','markedPromoted','livenessResult','mutationAttempted','clearingStore','preservedCount','clearedCount','queuePendingWork','markPendingWork','unexpectedWeakRooting','sensitiveAllocations','eeSuspended','threadStoreLockHeld','managedEntryProhibited','restart','resume','safeStopReason')
            foreach ($field in $c32AgreeFields) {
                $values = @($runResults | ForEach-Object { Get-MarkerField $_.markerLine $field } | Select-Object -Unique)
                if ($values.Count -ne 1) { throw "C011EC32 semantic field $field varied across fresh boots." }
            }
            $manifest = [ordered]@{
                outcome=$firstC32Run.outcome; successLevel=$firstC32Run.successLevel; proofMode=$ProofMode; marker='C011EC32'; preflightMarker='C011EC32-PREFLIGHT'; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; upstream=$upstream; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState
                lockedRuntimeIdentity=[ordered]@{ nativeAot='9.0.0'; architecture='AMD64'; gc='Workstation'; gcInterfaces='5.3 / 2'; sourceCommit=$lockedCommit }
                weakHandleAllocation=[ordered]@{ managedApi='GCHandle.Alloc(target, GCHandleType.Weak)'; nativeEntry='RhpHandleAlloc'; nativeAllocator='CreateHandleOfType -> HndCreateHandle'; handleType=(Get-MarkerField $firstC32Run.markerLine 'handleType'); allocationEntryAddress=(Get-MarkerField $firstC32Run.markerLine 'allocationEntryAddress'); allocationCount=(Get-MarkerField $firstC32Run.markerLine 'allocationCount'); target=(Get-MarkerField $firstC32Run.markerLine 'target'); targetType=(Get-MarkerField $firstC32Run.markerLine 'targetType'); helperReturnAddress=(Get-MarkerField $firstC32Run.markerLine 'helperReturnAddress'); helperReturned=(Get-MarkerField $firstC32Run.markerLine 'helperReturned'); weakHandleSlot=(Get-MarkerField $firstC32Run.markerLine 'weakHandleSlot'); weakHandleValueBefore=(Get-MarkerField $firstC32Run.markerLine 'weakHandleValueBefore') }
                rootAudit=[ordered]@{ strongRootMatches=(Get-MarkerField $firstC32Run.markerLine 'strongRootMatches'); stackRootMatches=(Get-MarkerField $firstC32Run.markerLine 'stackRootMatches'); registerRootMatches=(Get-MarkerField $firstC32Run.markerLine 'registerRootMatches'); ordinaryRootMatches=(Get-MarkerField $firstC32Run.markerLine 'ordinaryRootMatches'); staticThreadStaticRootMatches=(Get-MarkerField $firstC32Run.markerLine 'staticThreadStaticRootMatches'); threadAbortRootMatches=(Get-MarkerField $firstC32Run.markerLine 'threadAbortRootMatches'); strongHandleMatches=(Get-MarkerField $firstC32Run.markerLine 'strongHandleMatches'); graphDerivedPromotions=(Get-MarkerField $firstC32Run.markerLine 'graphDerivedPromotions'); targetQueueInsertions=(Get-MarkerField $firstC32Run.markerLine 'targetQueueInsertions'); targetChildDiscoveries=(Get-MarkerField $firstC32Run.markerLine 'targetChildDiscoveries'); targetMarkWrites=(Get-MarkerField $firstC32Run.markerLine 'targetMarkWrites'); unexpectedWeakRooting=(Get-MarkerField $firstC32Run.markerLine 'unexpectedWeakRooting') }
                topology=[ordered]@{ bucketIndex=(Get-MarkerField $firstC32Run.markerLine 'bucketIndex'); table=(Get-MarkerField $firstC32Run.markerLine 'table'); segment=(Get-MarkerField $firstC32Run.markerLine 'segment'); block=(Get-MarkerField $firstC32Run.markerLine 'block'); blockFirstSlot=(Get-MarkerField $firstC32Run.markerLine 'blockFirstSlot'); blockType=(Get-MarkerField $firstC32Run.markerLine 'blockType'); blockIndex=(Get-MarkerField $firstC32Run.markerLine 'blockIndex'); slot=(Get-MarkerField $firstC32Run.markerLine 'slot'); slotIndex=(Get-MarkerField $firstC32Run.markerLine 'slotIndex'); slotsInspected=(Get-MarkerField $firstC32Run.markerLine 'slotsInspected'); shortWeakSlots=(Get-MarkerField $firstC32Run.markerLine 'shortWeakSlots'); nonNullShortWeakHandles=(Get-MarkerField $firstC32Run.markerLine 'nonNullShortWeakHandles'); proofHandleMatched=(Get-MarkerField $firstC32Run.markerLine 'proofHandleMatched') }
                liveness=[ordered]@{ callbackFunction=(Get-MarkerField $firstC32Run.markerLine 'livenessCallbackFunction'); callbackEntry=(Get-MarkerField $firstC32Run.markerLine 'livenessCallbackEntry'); decisionAddress=(Get-MarkerField $firstC32Run.markerLine 'livenessDecisionAddress'); condemnedGeneration=(Get-MarkerField $firstC32Run.markerLine 'condemnedGeneration'); targetGeneration=(Get-MarkerField $firstC32Run.markerLine 'targetGeneration'); markWordAddress=(Get-MarkerField $firstC32Run.markerLine 'markWordAddress'); markWordBefore=(Get-MarkerField $firstC32Run.markerLine 'markWordBefore'); markMask=(Get-MarkerField $firstC32Run.markerLine 'markMask'); markStateBefore=(Get-MarkerField $firstC32Run.markerLine 'markStateBefore'); markedPromoted=(Get-MarkerField $firstC32Run.markerLine 'markedPromoted'); result=(Get-MarkerField $firstC32Run.markerLine 'livenessResult') }
                clearing=[ordered]@{ slotBefore=(Get-MarkerField $firstC32Run.markerLine 'slotBefore'); slotAfter=(Get-MarkerField $firstC32Run.markerLine 'slotAfter'); mutationAttempted=(Get-MarkerField $firstC32Run.markerLine 'mutationAttempted'); clearingStore=(Get-MarkerField $firstC32Run.markerLine 'clearingStore'); clearingStoreAddress=(Get-MarkerField $firstC32Run.markerLine 'clearingStoreAddress'); clearedCount=(Get-MarkerField $firstC32Run.markerLine 'clearedCount'); preservedCount=(Get-MarkerField $firstC32Run.markerLine 'preservedCount') }
                invariants=[ordered]@{ queuePendingWork=(Get-MarkerField $firstC32Run.markerLine 'queuePendingWork'); markPendingWork=(Get-MarkerField $firstC32Run.markerLine 'markPendingWork'); sensitiveAllocations=(Get-MarkerField $firstC32Run.markerLine 'sensitiveAllocations'); eeSuspended=(Get-MarkerField $firstC32Run.markerLine 'eeSuspended'); threadStoreLockHeld=(Get-MarkerField $firstC32Run.markerLine 'threadStoreLockHeld'); threadStoreLockOwner=(Get-MarkerField $firstC32Run.markerLine 'threadStoreLockOwner'); threadStoreRecursion=(Get-MarkerField $firstC32Run.markerLine 'threadStoreRecursion'); cooperative=(Get-MarkerField $firstC32Run.markerLine 'cooperative'); preemptive=(Get-MarkerField $firstC32Run.markerLine 'preemptive'); managedEntryProhibited=(Get-MarkerField $firstC32Run.markerLine 'managedEntryProhibited'); restart=(Get-MarkerField $firstC32Run.markerLine 'restart'); resume=(Get-MarkerField $firstC32Run.markerLine 'resume') }
                qemu=[ordered]@{ version=$qemuVersion; runCount=$FreshBootCount; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); evidenceRoot=$runRoot; exactCommandLog=(Join-Path $runRoot 'commands.txt'); runs=$runResults }
                payloadHashes=[ordered]@{ proofKernel=$specializedKernelHash; pe=(Hash-File $pePath); elf=(Hash-File $elfPath); map=(Hash-File $mapPath) }
                regressions=[ordered]@{ C011EC26_to_C011EC30='PASS chronology retained through authentic dead short-weak processing'; C011EC29='PASS AfterGcScanRoots return and mark closure retained'; C011EC30='PASS HandleTableMap topology and structural slot match retained'; converter='PASS PE to ELF conversion'; linkerAndSourceGuards='PASS linker/table validation and locked-source guards'; ordinaryBoot='PASS after finally restoration'; diffCheck='PASS git diff --check' }
                documentation='docs/dotnet/NATIVEAOT_WORKSTATION_GC_SHORT_WEAK_CLEARING.md'; evidenceRoot=$runRoot; manifestPath=$manifestPath; ordinaryRestoration=[ordered]@{ buildSha256=(Hash-File $kernelPath); espSha256=(Hash-File $espKernelPath); expectedSha256=$normalKernelHash; restoredByFinally=$true }
            }
            $manifest | ConvertTo-Json -Depth 60 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
            Write-Host "C011EC32 NativeAOT Workstation dead short-weak handle: Outcome C" -ForegroundColor Green
        }
    } elseif ($isC011EC31) {
        if (@($runResults).Count -ne $FreshBootCount) { throw "The C011EC31 live short-weak proof produced $(@($runResults).Count) runs instead of $FreshBootCount." }
        $blockedC31Runs = @($runResults | Where-Object { $_.safeStopMarker -eq 'C011EC31-BLOCKED' })
        $failedC31Runs = @($runResults | Where-Object { $_.safeStopMarker -ne 'C011EC31' -and $_.safeStopMarker -ne 'C011EC31-BLOCKED' })
        if ($failedC31Runs.Count -ne 0) { throw "The C011EC31 live short-weak proof contained an unclassified run failure." }
        if ($blockedC31Runs.Count -ne 0) {
            $manifest = [ordered]@{
                outcome='H / genuine short-weak liveness proof blocked'; proofMode=$ProofMode; marker='C011EC31-BLOCKED'; preflightMarker='C011EC31-PREFLIGHT'; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; upstream=$upstream; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState
                lockedRuntimeIdentity=[ordered]@{ nativeAot='9.0.0'; architecture='AMD64'; gc='Workstation'; gcInterfaces='5.3 / 2'; sourceCommit=$lockedCommit }
                qemu=[ordered]@{ version=$qemuVersion; runCount=$FreshBootCount; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); evidenceRoot=$runRoot; runs=$runResults }
                ordinaryRestoration=[ordered]@{ buildSha256=(Hash-File $kernelPath); espSha256=(Hash-File $espKernelPath); expectedSha256=$normalKernelHash; restoredByFinally=$true }; documentation='docs/dotnet/NATIVEAOT_WORKSTATION_GC_SHORT_WEAK_LIVENESS.md'; evidenceRoot=$runRoot; manifestPath=$manifestPath
            }
            $manifest | ConvertTo-Json -Depth 40 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
            Write-Host "C011EC31 live short-weak proof: blocker (Outcome H)" -ForegroundColor Yellow
        } else {
            $firstC31Run = $runResults[0]
            $c31AgreeFields = @('successLevel','c011ec31Preflight','c011ec29Preflight','allocationCount','weakHandleAllocationResult','weakHandleAllocationCallbacks','strongHandlePromotions','handleType','strongRootMatched','proofHandleMatched','bucketsVisited','tablesVisited','segmentsVisited','blocksVisited','slotsInspected','shortWeakSlots','nonNullShortWeakHandles','livenessCallbacks','livenessDecisions','liveDecisions','deadDecisions','markedPromoted','livenessResult','mutationAttempted','clearingStore','preservedCount','clearedCount','queuePendingWork','markPendingWork','unexpectedWeakRooting','sensitiveAllocations','eeSuspended','threadStoreLockHeld','managedEntryProhibited','restart','resume','safeStopReason')
            foreach ($field in $c31AgreeFields) {
                $values = @($runResults | ForEach-Object { Get-MarkerField $_.markerLine $field } | Select-Object -Unique)
                if ($values.Count -ne 1) { throw "C011EC31 semantic field $field varied across fresh boots." }
            }
            $manifest = [ordered]@{
                outcome=$firstC31Run.outcome; successLevel=$firstC31Run.successLevel; proofMode=$ProofMode; marker='C011EC31'; preflightMarker='C011EC31-PREFLIGHT'; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; upstream=$upstream; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState
                lockedRuntimeIdentity=[ordered]@{ nativeAot='9.0.0'; architecture='AMD64'; gc='Workstation'; gcInterfaces='5.3 / 2'; sourceCommit=$lockedCommit }
                weakHandleAllocation=[ordered]@{ api='GCHandle.Alloc(target, GCHandleType.Weak)'; nativeEntry='RhpHandleAlloc'; handleType=(Get-MarkerField $firstC31Run.markerLine 'handleType'); allocationEntryAddress=(Get-MarkerField $firstC31Run.markerLine 'allocationEntryAddress'); target=(Get-MarkerField $firstC31Run.markerLine 'target'); strongRootSlot=(Get-MarkerField $firstC31Run.markerLine 'strongRootSlot'); strongRootValueBefore=(Get-MarkerField $firstC31Run.markerLine 'strongRootValueBefore'); weakHandleSlot=(Get-MarkerField $firstC31Run.markerLine 'weakHandleSlot'); weakHandleValueBefore=(Get-MarkerField $firstC31Run.markerLine 'weakHandleValueBefore'); strongHandlePromotions=(Get-MarkerField $firstC31Run.markerLine 'strongHandlePromotions') }
                topology=[ordered]@{ table=(Get-MarkerField $firstC31Run.markerLine 'table'); segment=(Get-MarkerField $firstC31Run.markerLine 'segment'); block=(Get-MarkerField $firstC31Run.markerLine 'block'); blockFirstSlot=(Get-MarkerField $firstC31Run.markerLine 'blockFirstSlot'); blockType=(Get-MarkerField $firstC31Run.markerLine 'blockType'); blockIndex=(Get-MarkerField $firstC31Run.markerLine 'blockIndex'); slot=(Get-MarkerField $firstC31Run.markerLine 'slot'); slotIndex=(Get-MarkerField $firstC31Run.markerLine 'slotIndex'); slotsInspected=(Get-MarkerField $firstC31Run.markerLine 'slotsInspected') }
                liveness=[ordered]@{ callbackFunction=(Get-MarkerField $firstC31Run.markerLine 'livenessCallbackFunction'); callbackEntry=(Get-MarkerField $firstC31Run.markerLine 'livenessCallbackEntry'); decisionAddress=(Get-MarkerField $firstC31Run.markerLine 'livenessDecisionAddress'); condemnedGeneration=(Get-MarkerField $firstC31Run.markerLine 'condemnedGeneration'); targetGeneration=(Get-MarkerField $firstC31Run.markerLine 'targetGeneration'); markWordAddress=(Get-MarkerField $firstC31Run.markerLine 'markWordAddress'); markWordBefore=(Get-MarkerField $firstC31Run.markerLine 'markWordBefore'); markMask=(Get-MarkerField $firstC31Run.markerLine 'markMask'); markStateBefore=(Get-MarkerField $firstC31Run.markerLine 'markStateBefore'); result=(Get-MarkerField $firstC31Run.markerLine 'livenessResult') }
                preservation=[ordered]@{ slotBefore=(Get-MarkerField $firstC31Run.markerLine 'slotBefore'); slotAfter=(Get-MarkerField $firstC31Run.markerLine 'slotAfter'); mutationAttempted=(Get-MarkerField $firstC31Run.markerLine 'mutationAttempted'); clearingStore=(Get-MarkerField $firstC31Run.markerLine 'clearingStore'); preservedCount=(Get-MarkerField $firstC31Run.markerLine 'preservedCount'); clearedCount=(Get-MarkerField $firstC31Run.markerLine 'clearedCount') }
                invariants=[ordered]@{ queuePendingWork=(Get-MarkerField $firstC31Run.markerLine 'queuePendingWork'); markPendingWork=(Get-MarkerField $firstC31Run.markerLine 'markPendingWork'); unexpectedWeakRooting=(Get-MarkerField $firstC31Run.markerLine 'unexpectedWeakRooting'); sensitiveAllocations=(Get-MarkerField $firstC31Run.markerLine 'sensitiveAllocations'); eeSuspended=(Get-MarkerField $firstC31Run.markerLine 'eeSuspended'); threadStoreLockHeld=(Get-MarkerField $firstC31Run.markerLine 'threadStoreLockHeld'); managedEntryProhibited=(Get-MarkerField $firstC31Run.markerLine 'managedEntryProhibited'); restart=(Get-MarkerField $firstC31Run.markerLine 'restart'); resume=(Get-MarkerField $firstC31Run.markerLine 'resume') }
                qemu=[ordered]@{ version=$qemuVersion; runCount=$FreshBootCount; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); evidenceRoot=$runRoot; exactCommandLog=(Join-Path $runRoot 'commands.txt'); runs=$runResults }
                payloadHashes=[ordered]@{ proofKernel=$specializedKernelHash; pe=(Hash-File $pePath); elf=(Hash-File $elfPath); map=(Hash-File $mapPath) }
                regressions=[ordered]@{ C011EC26_to_C011EC30='PASS chronology retained through authentic short-weak processing'; converter='PASS PE to ELF conversion'; sourceGuards='PASS locked source injection and symbol audit'; ordinaryBoot='PASS after finally restoration'; diffCheck='PASS git diff --check' }
                documentation='docs/dotnet/NATIVEAOT_WORKSTATION_GC_SHORT_WEAK_LIVENESS.md'; evidenceRoot=$runRoot; manifestPath=$manifestPath; ordinaryRestoration=[ordered]@{ buildSha256=(Hash-File $kernelPath); espSha256=(Hash-File $espKernelPath); expectedSha256=$normalKernelHash; restoredByFinally=$true }
            }
            $manifest | ConvertTo-Json -Depth 60 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
            Write-Host "C011EC31 NativeAOT Workstation live short-weak handle: Outcome C" -ForegroundColor Green
        }
    } elseif ($isC011EC30) {
        if (@($runResults).Count -ne $FreshBootCount) { throw "The C011EC30 short-weak operation produced $(@($runResults).Count) runs instead of $FreshBootCount." }
        $blockedC30Runs = @($runResults | Where-Object { $_.safeStopMarker -eq 'C011EC30-BLOCKED' })
        $failedC30Runs = @($runResults | Where-Object { $_.safeStopMarker -ne 'C011EC30' -and $_.safeStopMarker -ne 'C011EC30-BLOCKED' })
        if ($failedC30Runs.Count -ne 0) { throw "The C011EC30 short-weak operation contained an unclassified run failure." }
        if ($blockedC30Runs.Count -ne 0) {
            $manifest = [ordered]@{
                outcome='H / authentic short-weak handle-table operation was blocked'; proofMode=$ProofMode; marker='C011EC30-BLOCKED'; preflightMarker='C011EC30-PREFLIGHT'; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; upstream=$upstream; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState
                lockedRuntimeIdentity=[ordered]@{ nativeAot='9.0.0'; architecture='AMD64'; gc='Workstation'; gcInterfaces='5.3 / 2'; sourceCommit=$lockedCommit }
                qemu=[ordered]@{ version=$qemuVersion; runCount=$FreshBootCount; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); evidenceRoot=$runRoot; runs=$runResults }
                ordinaryRestoration=[ordered]@{ buildSha256=(Hash-File $kernelPath); espSha256=(Hash-File $espKernelPath); expectedSha256=$normalKernelHash; restoredByFinally=$true }; documentation='docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_SHORT_WEAK_HANDLE_OPERATION.md'; evidenceRoot=$runRoot; manifestPath=$manifestPath
            }
            $manifest | ConvertTo-Json -Depth 40 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
            Write-Host "C011EC30 short-weak handle operation: blocker (Outcome H)" -ForegroundColor Yellow
        } else {
            $firstC30Run = $runResults[0]
            $c30AgreeFields = @('successLevel','c011ec30Preflight','c011ec29Preflight','handleScanEntries','handleMapReads','bucketsVisited','tablesVisited','segmentsVisited','blocksVisited','slotsInspected','candidateHandles','livenessDecisions','liveDecisions','deadDecisions','mutationAttempts','clearedCount','preservedCount','diagnosticMutationCount','noHandleCompletion','condemnedGeneration','maximumGeneration','handleScanFlags','firstTargetMarked','firstTargetInCondemnedGeneration','eeSuspended','threadStoreLockHeld','managedEntryProhibited','restart','resume','safeStopReason')
            foreach ($field in $c30AgreeFields) {
                $values = @($runResults | ForEach-Object { Get-MarkerField $_.markerLine $field } | Select-Object -Unique)
                if ($values.Count -ne 1) { throw "C011EC30 semantic field $field varied across fresh boots." }
            }
            $manifest = [ordered]@{
                outcome=$firstC30Run.outcome; successLevel=$firstC30Run.successLevel; proofMode=$ProofMode; marker='C011EC30'; preflightMarker='C011EC30-PREFLIGHT'; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; upstream=$upstream; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState
                lockedRuntimeIdentity=[ordered]@{ nativeAot='9.0.0'; architecture='AMD64'; gc='Workstation'; gcInterfaces='5.3 / 2'; sourceCommit=$lockedCommit }
                chronology=[ordered]@{ prior='C011EC28 queue closure'; postMark='C011EC29 AfterGcScanRoots return -> GCScan::GcShortWeakPtrScan -> Ref_CheckAlive'; mapSource='locked src/coreclr/gc/objecthandle.cpp:1523-1566'; scanSource='locked src/coreclr/gc/handletablescan.cpp:1375-1740'; livenessSource='locked src/coreclr/gc/objecthandle.cpp:339-365'; promotionRule='locked src/coreclr/gc/gc.cpp:49187 GCHeap::IsPromoted; header mark bit is observed read-only' }
                topology=[ordered]@{ mapAddress=(Get-MarkerField $firstC30Run.markerLine 'mapAddress'); bucketsFieldAddress=(Get-MarkerField $firstC30Run.markerLine 'bucketsFieldAddress'); bucketsValue=(Get-MarkerField $firstC30Run.markerLine 'bucketsValue'); maxIndex=(Get-MarkerField $firstC30Run.markerLine 'maxIndex'); bucketsVisited=(Get-MarkerField $firstC30Run.markerLine 'bucketsVisited'); tablesVisited=(Get-MarkerField $firstC30Run.markerLine 'tablesVisited'); segmentsVisited=(Get-MarkerField $firstC30Run.markerLine 'segmentsVisited'); blocksVisited=(Get-MarkerField $firstC30Run.markerLine 'blocksVisited'); slotsInspected=(Get-MarkerField $firstC30Run.markerLine 'slotsInspected') }
                operation=[ordered]@{ candidateHandles=(Get-MarkerField $firstC30Run.markerLine 'candidateHandles'); livenessChecks=(Get-MarkerField $firstC30Run.markerLine 'livenessChecks'); livenessDecisions=(Get-MarkerField $firstC30Run.markerLine 'livenessDecisions'); liveDecisions=(Get-MarkerField $firstC30Run.markerLine 'liveDecisions'); deadDecisions=(Get-MarkerField $firstC30Run.markerLine 'deadDecisions'); mutationAttempts=(Get-MarkerField $firstC30Run.markerLine 'mutationAttempts'); clearedCount=(Get-MarkerField $firstC30Run.markerLine 'clearedCount'); preservedCount=(Get-MarkerField $firstC30Run.markerLine 'preservedCount'); firstSlotAddress=(Get-MarkerField $firstC30Run.markerLine 'firstSlotAddress'); firstSlotBefore=(Get-MarkerField $firstC30Run.markerLine 'firstSlotBefore'); firstSlotAfter=(Get-MarkerField $firstC30Run.markerLine 'firstSlotAfter'); firstTarget=(Get-MarkerField $firstC30Run.markerLine 'firstTarget'); firstMarkWordAddress=(Get-MarkerField $firstC30Run.markerLine 'firstMarkWordAddress'); firstMarkWordBefore=(Get-MarkerField $firstC30Run.markerLine 'firstMarkWordBefore'); firstDecisionAddress=(Get-MarkerField $firstC30Run.markerLine 'firstDecisionAddress'); firstTargetMarked=(Get-MarkerField $firstC30Run.markerLine 'firstTargetMarked'); firstTargetInCondemnedGeneration=(Get-MarkerField $firstC30Run.markerLine 'firstTargetInCondemnedGeneration'); noHandleCompletion=(Get-MarkerField $firstC30Run.markerLine 'noHandleCompletion'); diagnosticMutationCount=(Get-MarkerField $firstC30Run.markerLine 'diagnosticMutationCount') }
                invariants=[ordered]@{ eeSuspended=(Get-MarkerField $firstC30Run.markerLine 'eeSuspended'); threadStoreLockHeld=(Get-MarkerField $firstC30Run.markerLine 'threadStoreLockHeld'); threadStoreLockOwner=(Get-MarkerField $firstC30Run.markerLine 'threadStoreLockOwner'); threadStoreRecursion=(Get-MarkerField $firstC30Run.markerLine 'threadStoreRecursion'); managedEntryProhibited=(Get-MarkerField $firstC30Run.markerLine 'managedEntryProhibited'); queuePendingAtTransition=(Get-MarkerField $firstC30Run.markerLine 'queuePendingAtTransition'); markPendingAtTransition=(Get-MarkerField $firstC30Run.markerLine 'markPendingAtTransition'); restart=(Get-MarkerField $firstC30Run.markerLine 'restart'); resume=(Get-MarkerField $firstC30Run.markerLine 'resume') }
                qemu=[ordered]@{ version=$qemuVersion; runCount=$FreshBootCount; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); evidenceRoot=$runRoot; exactCommandLog=(Join-Path $runRoot 'commands.txt'); runs=$runResults }
                payloadHashes=[ordered]@{ proofKernel=$specializedKernelHash; pe=(Hash-File $pePath); elf=(Hash-File $elfPath); map=(Hash-File $mapPath) }
                regressions=[ordered]@{ C011EC29='PASS predecessor Outcome A retained'; C011EC28='PASS queue closure retained'; C011EC27='PASS mark/child graph retained'; sourceGuards='PASS locked source injection and symbol audit'; ordinaryBoot='PASS after finally restoration'; diffCheck='PASS git diff --check' }
                documentation='docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_SHORT_WEAK_HANDLE_OPERATION.md'; evidenceRoot=$runRoot; manifestPath=$manifestPath; ordinaryRestoration=[ordered]@{ buildSha256=(Hash-File $kernelPath); espSha256=(Hash-File $espKernelPath); expectedSha256=$normalKernelHash; restoredByFinally=$true }
            }
            $manifest | ConvertTo-Json -Depth 60 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
            Write-Host "C011EC30 NativeAOT Workstation first short-weak handle operation: Outcome A" -ForegroundColor Green
        }
    } elseif ($isC011EC29) {
        if (@($runResults).Count -ne $FreshBootCount) {
            throw "The C011EC29 post-mark experiment produced $(@($runResults).Count) runs instead of $FreshBootCount."
        }
        $blockedC29Runs = @($runResults | Where-Object { $_.safeStopMarker -eq 'C011EC29-BLOCKED' })
        $failedC29Runs = @($runResults | Where-Object { $_.safeStopMarker -ne 'C011EC29' -and $_.safeStopMarker -ne 'C011EC29-BLOCKED' })
        if ($failedC29Runs.Count -ne 0) { throw "The C011EC29 post-mark experiment contained an unclassified run failure." }
        if ($blockedC29Runs.Count -ne 0) {
            $manifest = [ordered]@{
                outcome='H / post-mark invariant or production contract blocker after the mark queue closed'; proofMode=$ProofMode; marker='C011EC29-BLOCKED'; preflightMarker='C011EC29-PREFLIGHT'
                repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; upstream=$upstream; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState
                lockedRuntimeIdentity=[ordered]@{ nativeAot='9.0.0'; architecture='AMD64'; gc='Workstation'; gcInterfaces='5.3 / 2'; sourceCommit=$lockedCommit }
                qemu=[ordered]@{ version=$qemuVersion; runCount=$FreshBootCount; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); evidenceRoot=$runRoot; runs=$runResults }
                ordinaryRestoration=[ordered]@{ buildSha256=(Hash-File $kernelPath); espSha256=(Hash-File $espKernelPath); expectedSha256=$normalKernelHash; restoredByFinally=$true }
                documentation='docs/dotnet/NATIVEAOT_WORKSTATION_GC_POST_MARK_SHORT_WEAK_HANDLE_BOUNDARY.md'; evidenceRoot=$runRoot; manifestPath=$manifestPath
            }
            $manifest | ConvertTo-Json -Depth 40 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
            Write-Host "C011EC29 post-mark phase: blocker (Outcome H)" -ForegroundColor Yellow
        } else {
            $firstC29Run = $runResults[0]
            $c29AgreeFields = @('successLevel','afterGcScanRootsEntries','afterGcScanRootsReturns','nextPhaseEntries','shortWeakHandleScanEntries','handleMapReads','firstHandleTableMapAddress','firstHandleTableMapBucketsFieldAddress','firstReadValue','firstHandleTableMapMaxIndex','firstMutationAttempted','condemnedGeneration','maximumGeneration','generationCount','heapNumber','heapCount','collectionReason','compacting','relocating','promotion','fullCollection','finalizationReached','planReached','sweepReached','relocationReached','restartPreparationReached','eeSuspended','threadStoreLockHeld','threadStoreLockOwner','threadStoreRecursion','cooperative','preemptive','managedEntryProhibited','managedEntryAttempts','sensitiveAllocations','stackBoundsConsumed','queuePendingAtTransition','markPendingAtTransition','restart','resume','safeStopReason')
            foreach ($field in $c29AgreeFields) {
                $values = @($runResults | ForEach-Object { Get-MarkerField $_.markerLine $field } | Select-Object -Unique)
                if ($values.Count -ne 1) { throw "C011EC29 semantic field $field varied across fresh boots." }
            }
            $manifest = [ordered]@{
                outcome=$firstC29Run.outcome; successLevel=$firstC29Run.successLevel; proofMode=$ProofMode; marker='C011EC29'; preflightMarker='C011EC29-PREFLIGHT'
                repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; upstream=$upstream; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState
                lockedRuntimeIdentity=[ordered]@{ nativeAot='9.0.0'; architecture='AMD64'; gc='Workstation'; gcInterfaces='5.3 / 2'; sourceCommit=$lockedCommit }
                c28StartingBoundary=[ordered]@{ marker='C011EC28'; finalQueueCount=(Get-MarkerField $firstC29Run.c28MarkerLine 'finalCount'); finalEmpty=(Get-MarkerField $firstC29Run.c28MarkerLine 'finalEmptyResult'); finalDrainEmpty=(Get-MarkerField $firstC29Run.c28MarkerLine 'finalDrainEmptyResult'); markWrites=(Get-MarkerField $firstC29Run.c28MarkerLine 'markWrites'); graphScans=(Get-MarkerField $firstC29Run.c28MarkerLine 'objectsScanned'); childReads=(Get-MarkerField $firstC29Run.c28MarkerLine 'referenceSlots'); queueInvariantFailures=(Get-MarkerField $firstC29Run.c28MarkerLine 'queueInvariantFailures') }
                chronology=[ordered]@{ finalDrain='gc_heap::drain_mark_queue returned normally'; afterGcScanRoots='GCToEEInterface::AfterGcScanRoots'; afterGcScanRootsSource='locked src/coreclr/nativeaot/Runtime/gcenv.ee.cpp:145-155'; nextCollectorStatement='GCScan::GcShortWeakPtrScan (condemned_gen_number, max_generation, &sc)'; nextCollectorSource='locked src/coreclr/gc/gc.cpp:30128'; shortWeakFunction='GCScan::GcShortWeakPtrScan -> Ref_CheckAlive'; handleSource='locked src/coreclr/gc/objecthandle.cpp:1523-1566'; safeStop='first HandleTableMap::pBuckets read' }
                collection=[ordered]@{ condemnedGeneration=(Get-MarkerField $firstC29Run.markerLine 'condemnedGeneration'); maximumGeneration=(Get-MarkerField $firstC29Run.markerLine 'maximumGeneration'); generationCount=(Get-MarkerField $firstC29Run.markerLine 'generationCount'); heapCount=(Get-MarkerField $firstC29Run.markerLine 'heapCount'); heapNumber=(Get-MarkerField $firstC29Run.markerLine 'heapNumber'); reason=(Get-MarkerField $firstC29Run.markerLine 'collectionReason'); compacting=(Get-MarkerField $firstC29Run.markerLine 'compacting'); relocating=(Get-MarkerField $firstC29Run.markerLine 'relocating'); promotion=(Get-MarkerField $firstC29Run.markerLine 'promotion'); fullCollection=(Get-MarkerField $firstC29Run.markerLine 'fullCollection') }
                firstPostMarkOperation=[ordered]@{ structure='HandleTableMap'; structureAddress=(Get-MarkerField $firstC29Run.markerLine 'firstHandleTableMapAddress'); field='pBuckets'; fieldAddress=(Get-MarkerField $firstC29Run.markerLine 'firstHandleTableMapBucketsFieldAddress'); firstReadValue=(Get-MarkerField $firstC29Run.markerLine 'firstReadValue'); maxIndex=(Get-MarkerField $firstC29Run.markerLine 'firstHandleTableMapMaxIndex'); operationAddress=(Get-MarkerField $firstC29Run.markerLine 'firstOperationAddress'); mutationAttempted=(Get-MarkerField $firstC29Run.markerLine 'firstMutationAttempted'); mutationBefore=(Get-MarkerField $firstC29Run.markerLine 'mutationBefore'); mutationAfter=(Get-MarkerField $firstC29Run.markerLine 'mutationAfter'); semanticMeaning='first production short-weak handle-map field read; no heap or handle mutation performed' }
                subsystemReach=[ordered]@{ shortWeakHandles='yes'; finalization='no'; dependentHandles='not newly entered after this stop'; plan='no'; sweep='no'; relocation='no'; restartPreparation='no' }
                invariants=[ordered]@{ eeSuspended=(Get-MarkerField $firstC29Run.markerLine 'eeSuspended'); threadStoreLockHeld=(Get-MarkerField $firstC29Run.markerLine 'threadStoreLockHeld'); threadStoreLockOwner=(Get-MarkerField $firstC29Run.markerLine 'threadStoreLockOwner'); threadStoreRecursion=(Get-MarkerField $firstC29Run.markerLine 'threadStoreRecursion'); cooperative=(Get-MarkerField $firstC29Run.markerLine 'cooperative'); preemptive=(Get-MarkerField $firstC29Run.markerLine 'preemptive'); managedEntryProhibited=(Get-MarkerField $firstC29Run.markerLine 'managedEntryProhibited'); managedEntryAttempts=(Get-MarkerField $firstC29Run.markerLine 'managedEntryAttempts'); sensitiveAllocations=(Get-MarkerField $firstC29Run.markerLine 'sensitiveAllocations'); queuePending=(Get-MarkerField $firstC29Run.markerLine 'queuePendingAtTransition'); markPending=(Get-MarkerField $firstC29Run.markerLine 'markPendingAtTransition'); restart=(Get-MarkerField $firstC29Run.markerLine 'restart'); resume=(Get-MarkerField $firstC29Run.markerLine 'resume') }
                stackBounds=[ordered]@{ stackBase=(Get-MarkerField $firstC29Run.markerLine 'stackBase'); stackLimit=(Get-MarkerField $firstC29Run.markerLine 'stackLimit'); scanContextStackLimit=(Get-MarkerField $firstC29Run.markerLine 'scanContextStackLimit'); consumed=(Get-MarkerField $firstC29Run.markerLine 'stackBoundsConsumed') }
                qemu=[ordered]@{ version=$qemuVersion; runCount=$FreshBootCount; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); evidenceRoot=$runRoot; exactCommandLog=(Join-Path $runRoot 'commands.txt'); runs=$runResults }
                payloadHashes=[ordered]@{ proofKernel=$specializedKernelHash; pe=(Hash-File $pePath); elf=(Hash-File $elfPath); map=(Hash-File $mapPath) }
                regressions=[ordered]@{ C19ToC28='retained'; queueClosure='PASS C28 authentic quiescence and normal drain return'; stackWalk='PASS C26 normal terminal completion'; markChild='PASS C27 genuine mark/child graph traversal'; nativeUnwind='PASS two genuine native unwinds, no third'; converter='PASS'; sourceGuards='PASS'; ordinaryBoot='PASS after restoration'; diffCheck='PASS' }
                documentation='docs/dotnet/NATIVEAOT_WORKSTATION_GC_POST_MARK_SHORT_WEAK_HANDLE_BOUNDARY.md'; evidenceRoot=$runRoot; manifestPath=$manifestPath; ordinaryRestoration=[ordered]@{ buildSha256=(Hash-File $kernelPath); espSha256=(Hash-File $espKernelPath); expectedSha256=$normalKernelHash; restoredByFinally=$true }
            }
            $manifest | ConvertTo-Json -Depth 50 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
            Write-Host "C011EC29 NativeAOT Workstation post-mark short-weak phase: Outcome A" -ForegroundColor Green
        }
    } elseif ($isC011EC28) {
        if (@($runResults).Count -ne $FreshBootCount) {
            throw "The C011EC28 mark-queue experiment produced $(@($runResults).Count) runs instead of $FreshBootCount."
        }
        $blockedC28Runs = @($runResults | Where-Object { $_.safeStopMarker -eq 'C011EC28-BLOCKED' })
        $failedC28Runs = @($runResults | Where-Object { $_.safeStopMarker -ne 'C011EC28' -and $_.safeStopMarker -ne 'C011EC28-BLOCKED' })
        if ($failedC28Runs.Count -ne 0) { throw "The C011EC28 mark-queue experiment contained an unclassified run failure." }
        if ($blockedC28Runs.Count -ne 0) {
            $manifest = [ordered]@{
                outcome='E / authentic Workstation mark-queue closure was not reached'; proofMode=$ProofMode; marker='C011EC28-BLOCKED'; preflightMarker='C011EC28-PREFLIGHT'
                repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; upstream=$upstream; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState
                lockedRuntimeIdentity=[ordered]@{ nativeAot='9.0.0'; architecture='AMD64'; gc='Workstation'; gcInterfaces='5.3 / 2'; sourceCommit=$lockedCommit }
                qemu=[ordered]@{ version=$qemuVersion; runCount=$FreshBootCount; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); evidenceRoot=$runRoot; runs=$runResults }
                ordinaryRestoration=[ordered]@{ buildSha256=(Hash-File $kernelPath); espSha256=(Hash-File $espKernelPath); expectedSha256=$normalKernelHash; restoredByFinally=$true }
                documentation='docs/dotnet/NATIVEAOT_WORKSTATION_GC_MARK_QUEUE_CLOSURE.md'; evidenceRoot=$runRoot; manifestPath=$manifestPath
            }
            $manifest | ConvertTo-Json -Depth 40 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
            Write-Host "C011EC28 mark-queue closure: authentic closure not reached (Outcome E)" -ForegroundColor Yellow
        } else {
            $firstC28Run = $runResults[0]
            $c28AgreeFields = @('firstObject','firstChildValue','queueCapacity','initialHead','initialTail','initialCount','finalHead','finalTail','finalCount','drainEntries','drainReturns','dequeueAttempts','successfulDequeues','enqueueAttempts','successfulEnqueues','alreadyMarkedSkips','wraps','displacements','queueFullCount','queueFullResolved','displacementResolved','displacementPending','maxOccupancy','queueFinalOccupancy','queueInvariantFailures','emptyTests','finalEmptyResult','finalDrainEmptyTests','finalDrainEmptyResult','markTests','alreadyMarked','newlyMarked','markWrites','objectsScanned','referenceSlots','nullReferences','nonNullReferences','childPromoteAttempts','childQueueMarkEntries','childQueueMarkReturns','childQueueInsertions','finalDequeuedObject','finalDequeuedIndex','finalObject','finalObjectMarkState','finalObjectNewlyMarked','finalObjectChildSlots','finalObjectChildEnqueues','laterObject','laterObjectMarkWordAddress','laterObjectMarkWordBefore','laterObjectMarkWordAfter','laterObjectMarkMask','nextProductionBoundary','nextProductionBoundaryAddress','eeSuspended','cooperative','preemptive','restart','resume')
            foreach ($field in $c28AgreeFields) {
                $values = @($runResults | ForEach-Object { Get-MarkerField $_.markerLine $field } | Select-Object -Unique)
                if ($values.Count -ne 1) { throw "C011EC28 semantic field $field varied across fresh boots." }
            }
            $manifest = [ordered]@{
                outcome=$firstC28Run.outcome; successLevel=$firstC28Run.successLevel; proofMode=$ProofMode; marker='C011EC28'; preflightMarker='C011EC28-PREFLIGHT'
                repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; upstream=$upstream; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState
                lockedRuntimeIdentity=[ordered]@{ nativeAot='9.0.0'; architecture='AMD64'; gc='Workstation'; gcInterfaces='5.3 / 2'; sourceCommit=$lockedCommit }
                c27StartingBoundary=[ordered]@{ c26='normal iterator/GCScanRoots completion retained'; firstObject=(Get-MarkerField $firstC28Run.markerLine 'firstObject'); firstChild=(Get-MarkerField $firstC28Run.markerLine 'firstChildValue'); firstMarkWord=(Get-MarkerField $firstC28Run.markerLine 'firstObjectMarkWordAddress'); firstMarkBefore=(Get-MarkerField $firstC28Run.markerLine 'firstObjectMarkWordBefore'); firstMarkAfter=(Get-MarkerField $firstC28Run.markerLine 'firstObjectMarkWordAfter'); firstMarkMask=(Get-MarkerField $firstC28Run.markerLine 'firstObjectMarkMask'); firstChildSlot=(Get-MarkerField $firstC28Run.markerLine 'firstChildSlot') }
                c26Boundary=[ordered]@{ completion='normal'; iteratorCompletionCount=(Get-MarkerField $firstC28Run.markerLine 'iteratorCompletionCount'); gcScanRootsEntries=(Get-MarkerField $firstC28Run.markerLine 'gcScanRootsEntries'); gcScanRootsReturns=(Get-MarkerField $firstC28Run.markerLine 'gcScanRootsReturns'); rootsAtIteratorTerminal=6; totalRoots=(Get-MarkerField $firstC28Run.markerLine 'totalRoots'); category3Roots=(Get-MarkerField $firstC28Run.markerLine 'category3Roots'); registerRoots=(Get-MarkerField $firstC28Run.markerLine 'registerRoots'); stackRoots=(Get-MarkerField $firstC28Run.markerLine 'stackRoots'); firstPostStackRootSource=(Get-MarkerField $firstC28Run.markerLine 'firstPostStackRootSource'); postStackRootSourceCount=(Get-MarkerField $firstC28Run.markerLine 'postStackRootSourceCount'); nativeUnwinds=(Get-MarkerField $firstC28Run.markerLine 'nativeUnwindCount'); thirdUnwindAttempts=(Get-MarkerField $firstC28Run.markerLine 'thirdUnwindAttempts') }
                queueSemantics=[ordered]@{ sourceDeclaration='locked src/coreclr/gc/gcpriv.h:1487-1504'; constructor='locked src/coreclr/gc/gc.cpp:27290-27301'; insertion='locked src/coreclr/gc/gc.cpp:27303-27335'; removal='locked src/coreclr/gc/gc.cpp:27373-27402'; type='uint8_t* slot_table[16]'; capacity=16; storedFields=@('slot_table[16]','curr_slot_index'); head='derived diagnostic alias of curr_slot_index; no stored head'; tail='derived diagnostic alias of curr_slot_index; no stored tail'; count='derived diagnostic occupancy counter; no stored count'; empty='get_next_marked scans at most slot_count slots and returns nullptr after all 16 are empty/already cleared'; full='no explicit full branch; queue_mark overwrites the current ring slot, then marks/disposes the displaced old object according to locked semantics'; wrap='(index + 1) % slot_count'; localGlobal='single-thread WKS build; no MULTIPLE_HEAPS, BACKGROUND_GC, or MH_SC_MARK work stealing path'; restart='newly discovered child queue_mark writes the ring and get_next_marked resumes scanning; no diagnostic restart' }
                chronology=[ordered]@{ initialHead=(Get-MarkerField $firstC28Run.markerLine 'initialHead'); initialTail=(Get-MarkerField $firstC28Run.markerLine 'initialTail'); initialCount=(Get-MarkerField $firstC28Run.markerLine 'initialCount'); initialCursor=(Get-MarkerField $firstC28Run.markerLine 'initialCursor'); dequeueAttempts=(Get-MarkerField $firstC28Run.markerLine 'dequeueAttempts'); successfulDequeues=(Get-MarkerField $firstC28Run.markerLine 'successfulDequeues'); enqueueAttempts=(Get-MarkerField $firstC28Run.markerLine 'enqueueAttempts'); successfulEnqueues=(Get-MarkerField $firstC28Run.markerLine 'successfulEnqueues'); wraps=(Get-MarkerField $firstC28Run.markerLine 'wraps'); displacements=(Get-MarkerField $firstC28Run.markerLine 'displacements'); queueFullCount=(Get-MarkerField $firstC28Run.markerLine 'queueFullCount'); queueFullResolved=(Get-MarkerField $firstC28Run.markerLine 'queueFullResolved'); maxOccupancy=(Get-MarkerField $firstC28Run.markerLine 'maxOccupancy'); finalHead=(Get-MarkerField $firstC28Run.markerLine 'finalHead'); finalTail=(Get-MarkerField $firstC28Run.markerLine 'finalTail'); finalCount=(Get-MarkerField $firstC28Run.markerLine 'finalCount'); emptyTests=(Get-MarkerField $firstC28Run.markerLine 'emptyTests'); finalEmptyResult=(Get-MarkerField $firstC28Run.markerLine 'finalEmptyResult'); finalDrainEmptyTests=(Get-MarkerField $firstC28Run.markerLine 'finalDrainEmptyTests'); finalDrainEmptyResult=(Get-MarkerField $firstC28Run.markerLine 'finalDrainEmptyResult'); invariantFailures=(Get-MarkerField $firstC28Run.markerLine 'queueInvariantFailures') }
                mark=[ordered]@{ tests=(Get-MarkerField $firstC28Run.markerLine 'markTests'); alreadyMarked=(Get-MarkerField $firstC28Run.markerLine 'alreadyMarked'); alreadyMarkedSkips=(Get-MarkerField $firstC28Run.markerLine 'alreadyMarkedSkips'); newlyMarked=(Get-MarkerField $firstC28Run.markerLine 'newlyMarked'); writes=(Get-MarkerField $firstC28Run.markerLine 'markWrites'); firstWord=(Get-MarkerField $firstC28Run.markerLine 'firstObjectMarkWordAddress'); firstBefore=(Get-MarkerField $firstC28Run.markerLine 'firstObjectMarkWordBefore'); firstAfter=(Get-MarkerField $firstC28Run.markerLine 'firstObjectMarkWordAfter'); firstMask=(Get-MarkerField $firstC28Run.markerLine 'firstObjectMarkMask'); laterObject=(Get-MarkerField $firstC28Run.markerLine 'laterObject'); laterWord=(Get-MarkerField $firstC28Run.markerLine 'laterObjectMarkWordAddress'); laterBefore=(Get-MarkerField $firstC28Run.markerLine 'laterObjectMarkWordBefore'); laterAfter=(Get-MarkerField $firstC28Run.markerLine 'laterObjectMarkWordAfter'); laterMask=(Get-MarkerField $firstC28Run.markerLine 'laterObjectMarkMask'); finalObject=(Get-MarkerField $firstC28Run.markerLine 'finalObject'); finalWord=(Get-MarkerField $firstC28Run.markerLine 'finalObjectMarkWordAddress'); finalBefore=(Get-MarkerField $firstC28Run.markerLine 'finalObjectMarkWordBefore'); finalAfter=(Get-MarkerField $firstC28Run.markerLine 'finalObjectMarkWordAfter'); finalMask=(Get-MarkerField $firstC28Run.markerLine 'finalObjectMarkMask') }
                scanning=[ordered]@{ objects=(Get-MarkerField $firstC28Run.markerLine 'objectsScanned'); referenceSlots=(Get-MarkerField $firstC28Run.markerLine 'referenceSlots'); nullReferences=(Get-MarkerField $firstC28Run.markerLine 'nullReferences'); nonNullReferences=(Get-MarkerField $firstC28Run.markerLine 'nonNullReferences'); firstParent=(Get-MarkerField $firstC28Run.markerLine 'firstScanParent'); firstMethodTable=(Get-MarkerField $firstC28Run.markerLine 'firstScanMethodTable'); firstChild=(Get-MarkerField $firstC28Run.markerLine 'firstScanFirstChild'); laterParent=(Get-MarkerField $firstC28Run.markerLine 'laterScanParent'); laterMethodTable=(Get-MarkerField $firstC28Run.markerLine 'laterScanMethodTable'); finalParent=(Get-MarkerField $firstC28Run.markerLine 'finalScanParent'); finalMethodTable=(Get-MarkerField $firstC28Run.markerLine 'finalScanMethodTable'); finalFirstChild=(Get-MarkerField $firstC28Run.markerLine 'finalScanFirstChild') }
                childDerived=[ordered]@{ promoteAttempts=(Get-MarkerField $firstC28Run.markerLine 'childPromoteAttempts'); queueMarkEntries=(Get-MarkerField $firstC28Run.markerLine 'childQueueMarkEntries'); queueMarkReturns=(Get-MarkerField $firstC28Run.markerLine 'childQueueMarkReturns'); queueInsertions=(Get-MarkerField $firstC28Run.markerLine 'childQueueInsertions'); rootPromote='separate C26/C27 root counters retained' }
                finalObject=[ordered]@{ queueSlot=(Get-MarkerField $firstC28Run.markerLine 'finalDequeuedSlot'); index=(Get-MarkerField $firstC28Run.markerLine 'finalDequeuedIndex'); object=(Get-MarkerField $firstC28Run.markerLine 'finalObject'); markState=(Get-MarkerField $firstC28Run.markerLine 'finalObjectMarkState'); newlyMarked=(Get-MarkerField $firstC28Run.markerLine 'finalObjectNewlyMarked'); childSlots=(Get-MarkerField $firstC28Run.markerLine 'finalObjectChildSlots'); newChildrenEnqueued=(Get-MarkerField $firstC28Run.markerLine 'finalObjectChildEnqueues') }
                closure=[ordered]@{ finalSuccessfulDequeueOrdinal=(Get-MarkerField $firstC28Run.markerLine 'successfulDequeues'); finalObject=(Get-MarkerField $firstC28Run.markerLine 'finalObject'); queueStateAfter=[ordered]@{ head=(Get-MarkerField $firstC28Run.markerLine 'finalHead'); tail=(Get-MarkerField $firstC28Run.markerLine 'finalTail'); count=(Get-MarkerField $firstC28Run.markerLine 'finalCount') }; finalChildEnqueues=(Get-MarkerField $firstC28Run.markerLine 'finalObjectChildEnqueues'); subsequentEmptyTest=(Get-MarkerField $firstC28Run.markerLine 'finalEmptyResult'); drainReturns=(Get-MarkerField $firstC28Run.markerLine 'drainReturns') }
                nextProductionBoundary=[ordered]@{ function='GCToEEInterface::AfterGcScanRoots'; source='locked src/coreclr/gc/gcenv.ee.cpp:145-155'; phase='first authentic post-mark callback boundary'; address=(Get-MarkerField $firstC28Run.markerLine 'nextProductionBoundaryAddress'); drain='gc_heap::drain_mark_queue returned normally before this boundary' }
                stackBounds=[ordered]@{ stackBase=(Get-MarkerField $firstC28Run.markerLine 'stackBase'); stackLimit=(Get-MarkerField $firstC28Run.markerLine 'stackLimit'); scanContextStackLimit=(Get-MarkerField $firstC28Run.markerLine 'scanContextStackLimit'); consumed=(Get-MarkerField $firstC28Run.markerLine 'stackBoundsConsumed') }
                invariants=[ordered]@{ queue=(Get-MarkerField $firstC28Run.markerLine 'queueInvariantFailures'); object=(Get-MarkerField $firstC28Run.markerLine 'objectInvariantFailures'); eeSuspended=(Get-MarkerField $firstC28Run.markerLine 'eeSuspended'); threadStoreLockHeld=(Get-MarkerField $firstC28Run.markerLine 'threadStoreLockHeld'); managedEntryProhibited=(Get-MarkerField $firstC28Run.markerLine 'managedEntryProhibited'); cooperative=(Get-MarkerField $firstC28Run.markerLine 'cooperative'); preemptive=(Get-MarkerField $firstC28Run.markerLine 'preemptive'); threadUnderCrawl=(Get-MarkerField $firstC28Run.markerLine 'threadUnderCrawl'); restart=(Get-MarkerField $firstC28Run.markerLine 'restart'); resume=(Get-MarkerField $firstC28Run.markerLine 'resume') }
                sensitivePath=[ordered]@{ heapAllocation=0; managedAllocation=0; dynamicString=0; collection=0; diagnosticQueueMutation=0; diagnosticMarkMutation=0; arbitraryHeapScan=0; arbitraryStackScan=0; managedReentry=0; schedulerTransition=0 }
                qemu=[ordered]@{ version=$qemuVersion; runCount=$FreshBootCount; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); evidenceRoot=$runRoot; exactCommandLog=(Join-Path $runRoot 'commands.txt'); runs=$runResults }
                payloadHashes=[ordered]@{ proofKernel=$specializedKernelHash; pe=(Hash-File $pePath); elf=(Hash-File $elfPath); map=(Hash-File $mapPath) }
                regressions=[ordered]@{ C19ToC27='retained'; stackCompletion='PASS C26 normal completion'; firstMarkChild='PASS C27 genuine first mark write and child read'; queueSemantics='PASS locked declaration/insertion/removal guards'; providerUnwind='PASS retained'; linkedPhysicalAlias='PASS retained'; kernelMainReturnSlot='PASS retained'; linkerTable='PASS retained'; converter='PASS retained'; powershellParse='PASS'; sourceGuards='PASS'; ordinaryKernelBuild='PASS'; ordinaryBoot='PASS after restoration'; diffCheck='PASS' }
                documentation='docs/dotnet/NATIVEAOT_WORKSTATION_GC_MARK_QUEUE_CLOSURE.md'; evidenceRoot=$runRoot; manifestPath=$manifestPath; ordinaryRestoration=[ordered]@{ buildSha256=(Hash-File $kernelPath); espSha256=(Hash-File $espKernelPath); expectedSha256=$normalKernelHash; restoredByFinally=$true }
            }
            $manifest | ConvertTo-Json -Depth 50 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
            Write-Host "C011EC28 NativeAOT Workstation mark queue closure: Outcome A" -ForegroundColor Green
        }
    } elseif ($isC011EC27Stop) {
        if (@($runResults).Count -ne $FreshBootCount -or @($runResults | Where-Object { $_.safeStopMarker -ne 'C011EC27' }).Count -ne 0) {
            throw "The C011EC27 post-root queue experiment did not produce $FreshBootCount successful C011EC27 runs."
        }
        $firstC27Run = $runResults[0]
        $c27Outcomes = @($runResults | ForEach-Object { $_.outcome } | Select-Object -Unique)
        $c27Levels = @($runResults | ForEach-Object { $_.successLevel } | Select-Object -Unique)
        if ($c27Outcomes.Count -ne 1 -or $c27Levels.Count -ne 1) { throw "C011EC27 semantic outcome did not agree across fresh boots." }
        foreach ($field in @('successLevel','queueItemsConsumed','consumedIndex','consumedSlot','consumedObject','markMask','markWordBefore','markWordAfter','markWrites','childReads','childSlot','childValue','queueInvariantFailures','objectInvariantFailures')) {
            $values = @($runResults | ForEach-Object { Get-MarkerField $_.markerLine $field } | Select-Object -Unique)
            if ($values.Count -ne 1) { throw "C011EC27 field $field varied across fresh boots." }
        }
        $manifest = [ordered]@{
            outcome=$firstC27Run.outcome
            successLevel=$firstC27Run.successLevel
            proofMode=$ProofMode; marker='C011EC27'; preflightMarker='C011EC27-PREFLIGHT'
            repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; upstream=$upstream; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState
            lockedRuntimeIdentity=[ordered]@{ nativeAot='9.0.0'; architecture='AMD64'; gc='Workstation'; gcInterfaces='5.3 / 2'; sourceCommit=$lockedCommit }
            c26Boundary=[ordered]@{ completion='retained'; iteratorCompletionCount=$firstC27Run.c26.iteratorCompletionCount; gcScanRootsReturns=$firstC27Run.c26.gcScanRootsReturns; rootsAtIteratorTerminal=6; rootsAtGcScanRootsReturn=$firstC27Run.c26.totalRoots; stackScanTotalRoots=$firstC27Run.c26.stackScanTotalRoots; firstPostStackRootSource=(Get-MarkerField $firstC27Run.c26MarkerLine 'firstPostStackRootSource'); postStackRootSourceCount=$firstC27Run.c26.postStackSourceCount; firstPostStackRoot='ThreadAbortException source code 3'; afterGcScanRoots='reached' }
            postRootControlFlow=[ordered]@{ caller='GCScan::GcScanRoots / locked src/coreclr/gc/gc.cpp:29899-30063'; rootDrain='gc_heap::mark_phase -> drain_mark_queue before finalization/handle/dependent roots'; afterGcScanRoots='GCToEEInterface::AfterGcScanRoots / locked src/coreclr/gc/gcenv.ee.cpp:145-155'; firstQueueConsumer='gc_heap::drain_mark_queue / locked src/coreclr/gc/gc.cpp:28054-28090'; markPhase='GCScan::GcScanRoots followed by mark_queue drain'; childScanner='go_through_object_cl in drain_mark_queue' }
            queue=[ordered]@{ declaration='mark_queue_t / locked src/coreclr/gc/gcpriv.h:1487-1504'; layout='MARK_PHASE_PREFETCH 16-slot uint8_t* slot_table ring plus curr_slot_index'; owner=$firstC27Run.queue.owner; base=$firstC27Run.queue.base; cursorBefore=$firstC27Run.queue.cursorBefore; consumedIndex=$firstC27Run.queue.consumedIndex; consumedSlot=$firstC27Run.queue.consumedSlot; consumedObject=$firstC27Run.queue.consumedObject; firstQueueInsertionObject=(Get-MarkerField $firstC27Run.markerLine 'firstQueueInsertionObject'); sourceRootCategory=$firstC27Run.queue.sourceCategory; queueItemsConsumed=(Get-MarkerField $firstC27Run.markerLine 'queueItemsConsumed'); slotValueAfter=$firstC27Run.queue.slotValueAfter; cursorAfterConsumption=$firstC27Run.queue.cursorAfter; insertionsAtConsumed=$firstC27Run.queue.insertionsAtConsumed; insertionsAtAfter=$firstC27Run.queue.insertionsAtAfter; newQueueInsertion=$firstC27Run.queue.newInsertion }
            promoteChronology=[ordered]@{ stackDerived='4 attempts / 4 entries / 4 returns'; fullScan='6 entries / 5 returns'; firstRootProviderCategory=(Get-MarkerField $firstC27Run.markerLine 'firstRootProviderCategory'); firstRootValue=(Get-MarkerField $firstC27Run.markerLine 'firstRootValue'); postStackThreadAbort='one normal source, source code 3; roots 6 -> 7' }
            mark=[ordered]@{ representation='CObjectHeader raw method-table word GC_MARKED bit'; markedMacro='locked src/coreclr/gc/gc.cpp:11587'; isMarked='locked gc.cpp:4789-4792'; setMarked='locked gc.cpp:4783-4787'; bitmapOrTable='none; this WKS configuration uses the object-header mark bit'; wordAddress=$firstC27Run.mark.wordAddress; wordBefore=$firstC27Run.mark.wordBefore; mask=$firstC27Run.mark.mask; stateBefore=$firstC27Run.mark.stateBefore; testResult=$firstC27Run.mark.test; writeAttempted=$firstC27Run.mark.writeAttempted; writes=(Get-MarkerField $firstC27Run.markerLine 'markWrites'); wordAfter=$firstC27Run.mark.wordAfter; newlyMarked=$firstC27Run.mark.newlyMarked }
            childTraversal=[ordered]@{ scanAttempted=$firstC27Run.child.scanAttempted; parent=$firstC27Run.child.parent; parentMethodTable=$firstC27Run.child.methodTable; firstChildSlot=$firstC27Run.child.slot; firstChildValue=$firstC27Run.child.value; childReads=$firstC27Run.child.reads; childPromoteAttempted=$firstC27Run.child.promoteAttempted; graphTraversal=(Get-MarkerField $firstC27Run.markerLine 'graphTraversal') }
            invariants=[ordered]@{ queue=$firstC27Run.invariants.queue; object=$firstC27Run.invariants.object; sentinel=(Get-MarkerField $firstC27Run.markerLine 'sentinel'); storageObject=(Get-MarkerField $firstC27Run.markerLine 'storageObject'); sentinelIntegrity='retained'; storageObjectIntegrity='retained'; stackBase=(Get-MarkerField $firstC27Run.markerLine 'stackBase'); stackLimit=(Get-MarkerField $firstC27Run.markerLine 'stackLimit'); scanContextStackLimit=(Get-MarkerField $firstC27Run.markerLine 'scanContextStackLimit'); boundsConsumed=(Get-MarkerField $firstC27Run.markerLine 'stackBoundsConsumed'); eeSuspended=(Get-MarkerField $firstC27Run.markerLine 'eeSuspended'); cooperative=(Get-MarkerField $firstC27Run.markerLine 'cooperative'); preemptive=(Get-MarkerField $firstC27Run.markerLine 'preemptive'); restart=(Get-MarkerField $firstC27Run.markerLine 'restart'); resume=(Get-MarkerField $firstC27Run.markerLine 'resume') }
            sensitivePath=[ordered]@{ allocations=0; dynamicStrings=0; collections=0; arbitraryHeapScan=0; arbitraryStackScan=0; managedReentry=0; schedulerTransition=0; eeState='suspended' }
            qemu=[ordered]@{ version=$qemuVersion; runCount=$FreshBootCount; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); evidenceRoot=$runRoot; exactCommandLog=(Join-Path $runRoot 'commands.txt'); runs=$runResults }
            payloadHashes=[ordered]@{ proofKernel=$specializedKernelHash; pe=(Hash-File $pePath); elf=(Hash-File $elfPath); map=(Hash-File $mapPath) }
            regressions=[ordered]@{ C19ToC26='retained'; iterator='PASS normal terminal completion'; gcScanRoots='PASS 1 entry / 1 return'; nativeUnwind='PASS two genuine native unwinds, no third'; converter='PASS'; sourceGuards='PASS'; ordinaryBoot='PASS after restoration'; diffCheck='PASS' }
            documentation='docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_MARK_PROCESSING.md'; evidenceRoot=$runRoot; manifestPath=$manifestPath; ordinaryRestoration=[ordered]@{ expectedBuildSha256=$normalKernelHash; expectedEspSha256=$normalKernelHash; restoredByFinally=$true }
        }
        $manifest | ConvertTo-Json -Depth 40 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "C011EC27 NativeAOT post-root queue/mark processing: $($firstC27Run.outcome)" -ForegroundColor Green
    } elseif ($isC011EC26) {
        if (@($runResults).Count -ne $FreshBootCount -or
            @($runResults | Where-Object { $_.safeStopMarker -ne 'C011EC26' -or $_.outcome -ne 'A' }).Count -ne 0) {
            throw "The C011EC26 stack-completion experiment did not produce $FreshBootCount stable Outcome A runs."
        }
        $firstC26Run = $runResults[0]
        $c26Outcomes = @($runResults | ForEach-Object { $_.outcome } | Select-Object -Unique)
        if ($c26Outcomes.Count -ne 1 -or $c26Outcomes[0] -ne 'A') {
            throw "C011EC26 did not produce a stable normal stack-walk completion outcome across fresh boots."
        }
        $terminalInput = [Convert]::ToUInt64($firstC26Run.terminal.inputPc.Substring(2), 16)
        $terminalLinked = [Convert]::ToUInt64($firstC26Run.terminal.linkedPc.Substring(2), 16)
        $terminalBeginRva = [Convert]::ToUInt64($firstC26Run.terminal.beginRva.Substring(2), 16)
        $physicalKernelBase = '0x' + ($terminalInput - $terminalBeginRva).ToString('X')
        $linkedKernelBase = '0x' + ($terminalLinked - $terminalBeginRva).ToString('X')
        $firstPostScanEvent = Get-MarkerField $firstC26Run.markerLine 'firstPostScanEvent'
        $manifest = [ordered]@{
            outcome='A / structurally registered _start.halt converted from the C011EC25 proof boundary into normal NativeAOT StackFrameIterator end-of-stack completion; stack provider and Thread::GcScanRoots returned; first post-scan boundary observed at GCToEEInterface::AfterGcScanRoots'
            proofMode=$ProofMode; marker='C011EC26'; preflightMarker='C011EC26-PREFLIGHT'; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; upstream=$upstream; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            lockedRuntimeIdentity=[ordered]@{ nativeAot='9.0.0'; architecture='AMD64'; gc='Workstation'; gcInterfaces='5.3 / 2'; sourceCommit=$lockedCommit; codeManager='CoffNativeCodeManager'; managedRange='[0x10001000,0x10050950)' }
            c011ec25Boundary=[ordered]@{ historicalOutcome='Outcome B'; historicalPhysicalReturnPC='0x355D101E'; historicalFinalPhysicalPC='0x355CF01E'; currentPhysicalKernelBase=$physicalKernelBase; currentLinkedKernelBase=$linkedKernelBase; ownership='_start.halt'; noPhysicalAddressHardCode=$true; noSyntheticHaltUnwind=$true }
            terminalBoundary=[ordered]@{ registrationSource='kernel/arch/amd64/boot.asm linker-exported __guidexos_native_terminal_start/end range'; linkedStart=$nativeEntryAudit.terminalStart; linkedEnd=$nativeEntryAudit.terminalEnd; selectedDescriptor=$firstC26Run.terminal; lookupAttempts=$firstC26Run.terminal.attempts; lookupResults='two UNWINDABLE native frames, then one TERMINAL structural boundary; unsupported nearby/arbitrary/managed PCs remain non-terminal'; linkedAndPhysicalAlias=$true; dynamicSymbolLookupDuringSuspension=$false; allocationsDuringSuspension=0 }
            iterator=[ordered]@{ state='m_ControlPC=0 / IsValid() == false'; completion=$firstC26Run.completion.iterator; noThirdUnwind=$true; completionResult='normal loop termination after CalculateCurrentMethodState classified _start.halt as terminal' }
            stackProvider=[ordered]@{ entries=$firstC26Run.completion.stackProviderEntries; returns=$firstC26Run.completion.stackProviderReturns; callbackReturned=$true }
            gcScanRoots=[ordered]@{ entries=$firstC26Run.completion.gcScanRootsEntries; returns=$firstC26Run.completion.gcScanRootsReturns; threadEntries=$firstC26Run.completion.threadGcScanRootsEntries; threadReturns=$firstC26Run.completion.threadGcScanRootsReturns; rootEnumerationComplete=$firstC26Run.completion.rootEnumerationComplete; normalControlFlow=$true }
            terminalLookup=[ordered]@{ inputPC=$firstC26Run.terminal.inputPc; selectedPC=$firstC26Run.terminal.selectedPc; linkedPC=$firstC26Run.terminal.linkedPc; physicalKernelBase=$physicalKernelBase; linkedKernelBase=$linkedKernelBase; physicalStart=$firstC26Run.terminal.executableStart; physicalEnd=$firstC26Run.terminal.executableEnd; beginRVA=$firstC26Run.terminal.beginRva; endRVA=$firstC26Run.terminal.endRva; RSP=$firstC26Run.terminal.rsp; attempts=$firstC26Run.terminal.attempts; successes=$firstC26Run.terminal.successes; classification=$firstC26Run.terminal.classification; descriptorValid=$firstC26Run.terminal.descriptorValid }
             roots=[ordered]@{ historicalBeforeC26=[ordered]@{ total=6; category3=4; register=3; stack=1 }; stackScan=$firstC26Run.stackScan; fullGcScan=$firstC26Run.roots }
             promoteChronology=[ordered]@{ historical='4 attempts / 4 entries / 4 returns'; stackScan=$firstC26Run.stackScan; fullGcScan=$firstC26Run.roots; stackDerived=$true; postStackSource='ThreadAbortException root source after the iterator loop' }
             queue=$firstC26Run.queue
             postStackRoots=[ordered]@{ firstSourceCode=(Get-MarkerField $firstC26Run.markerLine 'firstPostStackRootSource'); firstSource='Thread::GcScanRootsWorker ThreadAbortException root at locked thread.cpp:566-568'; sourceCount=(Get-MarkerField $firstC26Run.markerLine 'postStackRootSourceCount'); fullRootDelta='six roots at iterator terminal -> seven after the normal post-stack ThreadAbort source' }
            postScan=[ordered]@{ firstEventCode=$firstPostScanEvent; firstEvent='GCToEEInterface::AfterGcScanRoots entered'; postScanAddress=(Get-MarkerField $firstC26Run.markerLine 'postScanAddress'); firstQueueOperation=(Get-MarkerField $firstC26Run.markerLine 'firstPostScanQueueOperation'); nextGCFunction='GCToEEInterface::AfterGcScanRoots'; nextBoundary='first authentic post-GcScanRoots callback boundary; no forced queue drain or mark traversal' }
            graph=$firstC26Run.graph
            stackBounds=[ordered]@{ stackBase=(Get-MarkerField $firstC26Run.markerLine 'stackBase'); scanContextStackLimit=(Get-MarkerField $firstC26Run.markerLine 'scanContextStackLimit'); consumed=(Get-MarkerField $firstC26Run.markerLine 'stackBoundsConsumed') }
            invariants=[ordered]@{ threadStoreOwner='GC initiator Thread'; threadStoreRecursion='1 / retained'; threadStoreLockHeld=(Get-MarkerField $firstC26Run.markerLine 'threadStoreLockHeld'); eeSuspended=(Get-MarkerField $firstC26Run.markerLine 'eeSuspended'); cooperative=(Get-MarkerField $firstC26Run.markerLine 'cooperative'); preemptive=(Get-MarkerField $firstC26Run.markerLine 'preemptive'); threadUnderCrawl=(Get-MarkerField $firstC26Run.markerLine 'threadUnderCrawl'); managedEntryProhibited=(Get-MarkerField $firstC26Run.markerLine 'managedEntryProhibited'); currentEnumeratedInitiator='retained equal in C25 evidence'; restart=(Get-MarkerField $firstC26Run.markerLine 'restart'); resume=(Get-MarkerField $firstC26Run.markerLine 'resume') }
            sensitivePath=[ordered]@{ allocations=0; managedAllocations=0; stringAllocations=0; dynamicCollections=0; registryMutation=0; arbitraryStackScan=0; managedReentry=0; schedulerTransition=0 }
            qemu=[ordered]@{ version=$qemuVersion; runCount=$FreshBootCount; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); exactCommandLog=(Join-Path $runRoot 'commands.txt'); evidenceRoot=$runRoot; runs=$runResults }
            payloadHashes=[ordered]@{ proofKernel=$specializedKernelHash; pe=(Hash-File $pePath); elf=(Hash-File $elfPath); map=(Hash-File $mapPath); serial=@($runResults | ForEach-Object { $_.serialSha256 }) }
            regressions=[ordered]@{ terminalProvider='PASS linked, physical-alias, nearby non-terminal, arbitrary metadata-less, normal unwindable, and managed-PC classifier tests'; nativeUnwind='PASS existing kernel provider, standalone helper, second-function, and native AMD64 unwind regressions'; c25ReturnSlot='PASS independent kernel_main return-slot guard retained'; converter='PASS PE-to-ELF conversion and linker/table validation'; sourceGuards='PASS locked runtime identity, linker terminal range, PowerShell parse, and source guards'; chronology='PASS C019-C025 evidence retained without relabeling'; ordinaryBoot='PASS ordinary boot smoke after restoration'; diffCheck='PASS git diff --check' }
            documentation='docs/dotnet/NATIVEAOT_WORKSTATION_GC_STACK_WALK_COMPLETION.md'; evidenceRoot=$runRoot; manifestPath=$manifestPath; ordinaryRestoration=[ordered]@{ expectedBuildSha256=$normalKernelHash; expectedEspSha256=$normalKernelHash; restoredByFinally=$true }
        }
        $manifest | ConvertTo-Json -Depth 40 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "C011EC26 NativeAOT stack-walk completion: Outcome A" -ForegroundColor Green
    } elseif ($isC011EC25) {
        if (@($runResults).Count -ne $FreshBootCount -or @($runResults | Where-Object { $_.safeStopMarker -ne 'C011EC25' }).Count -ne 0) {
            throw "The C011EC25 kernel-entry-boundary experiment did not produce $FreshBootCount C011EC25 runs."
        }
        $firstC25Run = $runResults[0]
        $c25Outcomes = @($runResults | ForEach-Object { $_.outcome } | Select-Object -Unique)
        if ($c25Outcomes.Count -ne 1 -or $c25Outcomes[0] -ne 'B') {
            throw "C011EC25 did not produce a stable Outcome B kernel-entry-boundary classification across fresh boots."
        }
        $manifest = [ordered]@{
            outcome='B / 0x355D101E is the physical alias of linked _start.halt at 0x10001E; kernel_main was called by _start, _start was entered by a non-returning loader/trampoline jmp, and boot_stack_top is the legitimate native stack bottom'
            proofMode=$ProofMode; marker='C011EC25'; preflightMarker='C011EC25-PREFLIGHT'; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; upstream=$upstream; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            lockedRuntimeIdentity=[ordered]@{ nativeAot='9.0.0'; architecture='AMD64'; gc='Workstation'; gcInterfaces='5.3 / 2'; sourceCommit=$lockedCommit; codeManager='CoffNativeCodeManager'; managedRange='[0x10001000,0x10050950)' }
            ordinaryBaseline=[ordered]@{ startingBuildSha256=$ordinaryKernelBefore.build; startingEspSha256=$ordinaryKernelBefore.esp; expectedSha256=$normalKernelHash }
            c011ec24Boundary=[ordered]@{ priorC24SuspendedPC='0x00000000001AE445'; priorC24FirstOutputRip='0x00000000356767AA'; priorC24FirstOutputRsp='0x0000000004E95F40'; priorC24SecondOutputRip='0x00000000355D101E'; priorC24SecondOutputRsp='0x0000000004E96000'; helper='runFirstRealAllocationImpl'; helperSuspendedPC=(Get-MarkerField $firstC25Run.markerLine 'inputRIP'); helperInputRSP=(Get-MarkerField $firstC25Run.markerLine 'inputRSP'); helperReturnSlot=(Get-MarkerField $firstC25Run.markerLine 'returnSlot'); helperReturnValue=(Get-MarkerField $firstC25Run.markerLine 'returnValue'); firstOutputRip=(Get-MarkerField $firstC25Run.markerLine 'outputRIP'); firstOutputRsp=(Get-MarkerField $firstC25Run.markerLine 'outputRSP'); secondInputRip=(Get-MarkerField $firstC25Run.markerLine 'secondInputRIP'); secondInputRsp=(Get-MarkerField $firstC25Run.markerLine 'secondInputRSP'); secondModuleBase=(Get-MarkerField $firstC25Run.markerLine 'secondModuleBase'); secondRuntimeFunction=(Get-MarkerField $firstC25Run.markerLine 'secondRuntimeFunction'); secondUnwindInfo=(Get-MarkerField $firstC25Run.markerLine 'secondUnwindInfo'); secondOutputRip=(Get-MarkerField $firstC25Run.markerLine 'secondOutputRIP'); secondOutputRsp=(Get-MarkerField $firstC25Run.markerLine 'secondOutputRSP'); secondUnwind='independently decoded and production-agreeing' }
            nativeEntryBoundary=$firstC25Run.boundary; secondUnwind=$firstC25Run.unwind; caller=$firstC25Run.caller; roots=$firstC25Run.roots; accounting=$firstC25Run.accounting; staticEntryAudit=$nativeEntryAudit; runs=$runResults
            startupContract=[ordered]@{ bootloaderEntry='LoadElf entry physical address -> BootHandoffTrampoline'; trampolineTransfer='mov rsp,stackTop; and rsp,~0xF; sub rsp,40; jmp r12'; kernelEntry='_start at linked image base'; kernelMainCall='_start+0x19 call kernel_main; return address _start.halt at _start+0x1E'; entryReturn='no caller return slot because trampoline uses jmp and _start overwrites RSP'; entryMetadata='no RUNTIME_FUNCTION/UNWIND_INFO covers .boot'; bottom='output RSP equals boot_stack_top and no metadata covers _start.halt'; thirdUnwindAttempted=$false }
            managedSemantics=[ordered]@{ managedFrames=1; totalRoots=6; category3Roots=4; registerRoots=3; stackRoots=1; stackDerivedPromote='4 / 4 / 4'; queue='4 -> 5'; markWrites=0; childReads=0; graphTraversal=0; nativeManagedRoots=0; managedReentry=0 }
            stackBounds=[ordered]@{ stackBase='0x0000000000000000'; scanContextStackLimit='0x0000000000000000'; consumed=0 }
            sensitivePath=[ordered]@{ allocations=0; registrationAfterSuspension=0; tableConstructionAfterSuspension=0; stringsOrDynamicContainers=0; arbitraryStackScan=0; schedulerTransitions=0; managedReentry=0; thirdUnwindAttempted=0 }
            qemu=[ordered]@{ version=$qemuVersion; runCount=$FreshBootCount; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); exactCommandLog=(Join-Path $runRoot 'commands.txt'); evidenceRoot=$runRoot; runs=$runResults }
            payloadHashes=[ordered]@{ pe=(Hash-File $pePath); elf=(Hash-File $elfPath); proofKernel=$specializedKernelHash; map=(Hash-File $mapPath) }
            regressions=[ordered]@{ secondUnwind='PASS independent kernel_main opcode/return-slot derivation agrees with production'; provider='PASS linked and physical alias descriptors retained at capacity 2; both third-PC lookups miss metadata'; entry='PASS _start.halt assembly boundary and boot_stack_top bottom'; standalone='PASS existing helper and second-function standalone checks'; converter='PASS PE-to-ELF conversion and fixed-base validation'; sourceGuards='PASS locked runtime/source guards and PowerShell parse'; gcChronology='PASS one managed frame, six roots, four category-3, 3 register, 1 stack, Promote 4/4/4, queue 4 -> 5, mark 0, child reads 0, graph 0'; ordinaryBoot='PASS ordinary kernel rebuilt and boot-smoked; restored by finally'; diffCheck='PASS git diff --check' }
            documentation='docs/dotnet/NATIVEAOT_WORKSTATION_GC_KERNEL_ENTRY_UNWIND_BOUNDARY.md'; evidenceRoot=$runRoot; manifestPath=$manifestPath; ordinaryRestoration=[ordered]@{ expectedBuildSha256=$normalKernelHash; expectedEspSha256=$normalKernelHash; restoredByFinally=$true }
        }
        $manifest | ConvertTo-Json -Depth 40 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "C011EC25 kernel-entry boundary experiment: Outcome B" -ForegroundColor Green
    } elseif ($isC011EC24) {
        if (@($runResults).Count -ne $FreshBootCount -or @($runResults | Where-Object { $_.safeStopMarker -ne 'C011EC24' }).Count -ne 0) {
            throw "The C011EC24 caller-provenance experiment did not produce $FreshBootCount C011EC24 runs."
        }
        $firstC24Run = $runResults[0]
        $c24Outcomes = @($runResults | ForEach-Object { $_.outcome } | Select-Object -Unique)
        if ($c24Outcomes.Count -ne 1 -or $c24Outcomes[0] -ne 'C') {
            throw "C011EC24 did not produce a stable Outcome C second-native-unwind classification across fresh boots."
        }
        $manifest = [ordered]@{
            outcome='C / independent return-slot derivation agrees with the production AMD64 unwind; recovered caller 0x3567A7AA is the loader identity-mapped physical alias of linked kernel_main+0x12A, the alias range was validated as a second kernel descriptor, and exactly one second native unwind completed'
            proofMode=$ProofMode; marker='C011EC24'; preflightMarker='C011EC24-PREFLIGHT'; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; upstream=$upstream; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            lockedRuntimeIdentity=[ordered]@{ nativeAot='9.0.0'; architecture='AMD64'; gc='Workstation'; gcInterfaces='5.3 / 2'; sourceCommit=$lockedCommit; codeManager='CoffNativeCodeManager'; managedRange='[0x10001000,0x10050950)' }
            ordinaryBaseline=[ordered]@{ startingBuildSha256=$ordinaryKernelBefore.build; startingEspSha256=$ordinaryKernelBefore.esp; expectedSha256=$normalKernelHash }
            c011ec23Boundary=[ordered]@{ moduleBase='0x00100000'; pdata='0x00214000..0x0021D1C8'; xdata='0x0021D1C8..0x00224BDC'; runtimeFunctionCount=3110; helper='runFirstRealAllocationImpl'; helperInterval='[0x001ADDF0,0x001AFBE3)'; proofRIP='0x001AE6D5'; proofRSP='0x0000000004E91B80'; proofRBP='0x0000000004E91B70'; priorOutputRIP='0x000000003567A7AA'; priorOutputRSP='0x0000000004E91F40' }
            nativeHelperAudit=$nativeHelperAudit; unwind=$firstC24Run.unwind; caller=$firstC24Run.caller; roots=$firstC24Run.roots; accounting=$firstC24Run.accounting; runs=$runResults
            provenance=[ordered]@{ independentReturnSlot='return slot = live RSP + decoded stack advance'; helperMetadata='UWOP_ALLOC_LARGE(size=0x378) followed by UWOP_PUSH_NONVOL for RBX, RSI, RDI, RBP, R12, R13, R14, R15'; opcodeSupport='all opcodes used by helper are implemented by guideXOS primitive; no chained record'; outputAgreement='PASS across preflight and suspended walk'; callerValid='0x00000001'; callerKernelRange='0x00000001'; callerManagedRange='0x00000000'; linkedCaller='0x001A57AA'; physicalAlias='0x356767AA'; secondUnwind='PASS exactly once' }
            restoredRegisters=[ordered]@{ RBX='source address, pre-unwind value, and recovered value emitted in each C011EC24 SAFE_STOP'; RSI='source address, pre-unwind value, and recovered value emitted in each C011EC24 SAFE_STOP'; RDI='source address, pre-unwind value, and recovered value emitted in each C011EC24 SAFE_STOP'; RBP='source address, pre-unwind value, and recovered value emitted in each C011EC24 SAFE_STOP'; R12='source address, pre-unwind value, and recovered value emitted in each C011EC24 SAFE_STOP'; R13='source address, pre-unwind value, and recovered value emitted in each C011EC24 SAFE_STOP'; R14='source address, pre-unwind value, and recovered value emitted in each C011EC24 SAFE_STOP'; R15='source address, pre-unwind value, and recovered value emitted in each C011EC24 SAFE_STOP' }
            moduleAudit=[ordered]@{ kernel='caller is the authoritative identity-mapped physical kernel alias; linked kernel range and physical alias both validated'; managed='caller outside NativeAOT managed interval'; runtimeSupport='no separate runtime-support module'; loaderMappings='kernel physical load range is identity-mapped executable by guideXOSBootLoader paging.cpp'; secondModuleRegistered=$false; kernelAliasDescriptorRegistered=$true; secondProviderLookup=$firstC24Run.unwind.secondProviderLookupSucceeded; secondRuntimeFunction=$firstC24Run.unwind.secondRuntimeFunction; secondUnwindAttempted=$firstC24Run.unwind.secondProductionUnwindAttempted; secondUnwindResult=$firstC24Run.unwind.secondUnwindResult }
            sourceTrace=[ordered]@{ unwindPrimitive='tools/dotnet/runtime-pack/src/probes/guidexos_nativeaot_gc_startup_probe.cpp'; provider='kernel/core/native_unwind_provider.cpp'; proofPath='kernel/core/nativeaot_pal_qemu_test.cpp'; metadata='kernel/arch/amd64/linker.ld and converted PE .pdata/.xdata'; managedRange='locked NativeAOT RuntimeInstance code range' }
            sensitivePath=[ordered]@{ allocations=0; registrationAfterSuspension=0; tableConstructionAfterSuspension=0; stringsOrCollections=0; arbitraryStackScan=0; managedReentry=0; schedulerTransitions=0; nativeFramesNotManaged=$true }
            qemu=[ordered]@{ version=$qemuVersion; runCount=$FreshBootCount; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); exactCommandLog=(Join-Path $runRoot 'commands.txt'); evidenceRoot=$runRoot; runs=$runResults }
            payloadHashes=[ordered]@{ pe=(Hash-File $pePath); elf=(Hash-File $elfPath); proofKernel=$specializedKernelHash; map=(Hash-File $mapPath) }
            regressions=[ordered]@{ standaloneUnwind='PASS helper and second nontrivial metadata tests'; provider='PASS linked and physical-alias registration, deterministic lookup, table validation'; converter='PASS PE-to-ELF conversion and fixed-base validation'; sourceGuards='PASS locked runtime/source guards and PowerShell parse'; gcChronology='PASS one managed frame, six roots, four category-3, 3 register, 1 stack, Promote 4/4/4, queue 4 -> 5, mark 0, child reads 0, graph 0'; ordinaryBoot='PASS ordinary kernel rebuilt and boot-smoked; restored by finally'; diffCheck='PASS git diff --check' }
            documentation='docs/dotnet/NATIVEAOT_WORKSTATION_GC_SECOND_NATIVE_CALLER_PROVENANCE.md'; evidenceRoot=$runRoot; manifestPath=$manifestPath; ordinaryRestoration=[ordered]@{ expectedBuildSha256=$normalKernelHash; expectedEspSha256=$normalKernelHash; restoredByFinally=$true }
        }
        $manifest | ConvertTo-Json -Depth 40 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "C011EC24 caller provenance experiment: Outcome C" -ForegroundColor Green
    } elseif ($isC011EC23) {
        if (@($runResults).Count -ne $FreshBootCount -or @($runResults | Where-Object { $_.safeStopMarker -ne 'C011EC23' }).Count -ne 0) {
            throw "The C011EC23 native-unwind experiment did not produce $FreshBootCount C011EC23 runs."
        }
        $firstC23Run = $runResults[0]
        $c23Outcomes = @($runResults | ForEach-Object { $_.outcome } | Select-Object -Unique)
        if ($c23Outcomes.Count -ne 1 -or $c23Outcomes[0] -notin @('B','C')) {
            throw "C011EC23 did not produce a stable supported native-unwind outcome across fresh boots."
        }
        $finalOutcome = $c23Outcomes[0]
        $manifest = [ordered]@{
            outcome="$finalOutcome / genuine kernel native unwind provider crossed runFirstRealAllocationImpl"; proofMode=$ProofMode; marker='C011EC23'; preflightMarker='C011EC23-PREFLIGHT'; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            lockedRuntimeIdentity=[ordered]@{ nativeAot='9.0.0'; architecture='AMD64'; gc='Workstation'; gcInterfaces='5.3 / 2'; sourceCommit=$lockedCommit; codeManager='CoffNativeCodeManager'; managedRange='[0x10001000,0x10050950)' }
            ordinaryBaseline=[ordered]@{ startingBuildSha256=$ordinaryKernelBefore.build; startingEspSha256=$ordinaryKernelBefore.esp; expectedSha256=$normalKernelHash }
            nativeHelperAudit=$nativeHelperAudit; nativeProvider=$firstC23Run.unwind; caller=$firstC23Run.caller; roots=$firstC23Run.roots; accounting=$firstC23Run.accounting; runs=$runResults
            sourceTrace=[ordered]@{ linker='kernel/arch/amd64/linker.ld'; provider='kernel/core/native_unwind_provider.cpp; kernel/core/include/kernel/native_unwind_provider.h'; contract='tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_native_unwind_contract.h'; startup='kernel/core/nativeaot_pal_qemu_test.cpp and guidexos_nativeaot_gc_startup_platform_contract'; iterator='injected locked src/coreclr/nativeaot/Runtime/StackFrameIterator.cpp CalculateCurrentMethodState native branch'; unwindPrimitive='tools/dotnet/runtime-pack/src/probes/guidexos_nativeaot_gc_startup_probe.cpp:267-489' }
            sensitivePath=[ordered]@{ allocations=0; dynamicStrings=0; collections=0; registrationAfterSuspension=0; sortingAfterSuspension=0; arbitraryStackScans=0; largeDumps=0; nativeFramesNotManaged=$true }
            qemu=[ordered]@{ version=$qemuVersion; runCount=$FreshBootCount; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); exactCommandLog=(Join-Path $runRoot 'commands.txt'); evidenceRoot=$runRoot; runs=$runResults }
            payloadHashes=[ordered]@{ pe=(Hash-File $pePath); elf=(Hash-File $elfPath); proofKernel=$specializedKernelHash; map=(Hash-File $mapPath) }
            regressions=[ordered]@{ C011EC19='retained: four category-3 roots, six total roots, four stack Promote attempts/entries/returns'; C011EC20='retained genuine managed-to-native transition'; provider='PASS startup registration, bounded lookup, final table validation, genuine RtlVirtualUnwind'; converter='PASS PE-to-ELF converter invocation and fixed-base map validation'; sourceGuards='PASS locked-source injection guards and manifest checks'; ordinaryPayload='restored by finally' }
            ordinaryRestoration=[ordered]@{ expectedBuildSha256=$normalKernelHash; expectedEspSha256=$normalKernelHash; restoredByFinally=$true }
            documentation='docs/dotnet/NATIVEAOT_WORKSTATION_GC_KERNEL_NATIVE_UNWIND_PROVIDER.md'; evidenceRoot=$runRoot; manifestPath=$manifestPath
        }
        $manifest | ConvertTo-Json -Depth 40 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT kernel native unwind provider experiment: Outcome $finalOutcome / C011EC23" -ForegroundColor Green
    } elseif ($isC011EC21 -and -not $isC011EC23) {
        if (@($runResults).Count -ne $FreshBootCount -or @($runResults | Where-Object { $_.safeStopMarker -ne 'C011EC21' }).Count -ne 0) {
            throw "The C011EC21 native continuation experiment did not produce $FreshBootCount C011EC21 runs."
        }
        $firstC21Run = $runResults[0]
        if (@($runResults | Where-Object { $_.outcome -ne 'E' }).Count -ne 0) {
            throw "C011EC21 did not classify every fresh boot as the deterministic native-unwind metadata blocker Outcome E."
        }
        $manifest = [ordered]@{
            outcome='E / runFirstRealAllocationImpl is a legitimate native caller, but the native helper has no structural unwind metadata; C011EC21 also characterizes the next NativeAOT transition contract (success marker type 3)'
            proofMode=$ProofMode; marker='C011EC21'; preflightMarker='C011EC21-PREFLIGHT'; successType='3 / next NativeAOT transition contract identified'; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            lockedRuntimeIdentity=[ordered]@{ nativeAot='9.0.0'; architecture='AMD64'; gc='Workstation'; gcInterfaces='5.3 / 2'; sourceCommit=$lockedCommit; codeManager='CoffNativeCodeManager'; managedRange='[0x10001000,0x10050950)' }
            c011ec20Boundary=[ordered]@{ managedControlPC='0x0000000010001D3F'; managedMethod='ManagedMain'; managedMethodInterval='[0x10001C20,0x10001E84)'; outputRIP=$firstC21Run.unwind.outputRIP; outputRSP=$firstC21Run.unwind.outputRSP; outputRBP=$firstC21Run.unwind.outputRBP; establisherFrame=$firstC21Run.unwind.establisherFrame; previousTransition=$firstC21Run.transition.previousFrame }
            ordinaryBaseline=[ordered]@{ startingBuildSha256=$ordinaryKernelBefore.build; startingEspSha256=$ordinaryKernelBefore.esp; expectedSha256=$normalKernelHash }
            transition=$firstC21Run.transition; unwind=$firstC21Run.unwind; caller=$firstC21Run.caller; nativeFrameChain=$firstC21Run.nativeFrameChain; nativeContinuation=$firstC21Run.nativeContinuation
            managedReentry=[ordered]@{ found=$false; rip=$null; managedRange=$firstC21Run.caller.managedRange; codeManager=$firstC21Run.caller.codeManager; findMethodInfo='not attempted'; methodInterval=$null }
            managedStackBottom=[ordered]@{ proven=$false; reason='previous transition == 0 is not a stack-bottom proof; locked StackFrameIterator.cpp takes its ordinary native continuation branch and would require independently valid native unwind metadata or another transition contract' }
            transitionLinking=[ordered]@{ defect=$false; nullPredecessorMeaning='2 / no older transition record supplied; ordinary native unwind/classification continues'; reversePInvokeType='1'; transitionType='PInvokeTransitionFrame' }
            roots=$firstC21Run.roots; accounting=$firstC21Run.accounting; queue=$firstC21Run.queue; bounds=[ordered]@{ stackBase=(Get-MarkerField $firstC21Run.markerLine 'stackBase'); stackLimit=(Get-MarkerField $firstC21Run.markerLine 'stackLimit'); scanContextStackLimit=(Get-MarkerField $firstC21Run.markerLine 'scanContextStackLimit'); consumed=$firstC21Run.accounting.boundsConsumed }
            nativeHelperAudit=$nativeHelperAudit
            sourceTrace=[ordered]@{ iterator='locked src/coreclr/nativeaot/Runtime/StackFrameIterator.cpp:1529-1600, 1720, 1830, 1913-1948'; transitionUnwind='locked src/coreclr/nativeaot/Runtime/windows/CoffNativeCodeManager.cpp:651-711'; ordinaryAmd64Unwind='locked CoffNativeCodeManager.cpp:778-788, 830-842'; managedRange='locked RuntimeInstance.cpp:96-109'; findMethodInfo='locked CoffNativeCodeManager.cpp:254-295'; helperSource='kernel/core/nativeaot_pal_qemu_test.cpp:1915-2356 and 2386-2413; managed indirect call source line 2200'; nativeChain='kernel::main -> runFirstRealAllocation ABI wrapper -> runFirstRealAllocationImpl -> indirect ManagedMain call; public wrapper tail-jumps to the helper' }
            sensitivePath=[ordered]@{ allocations=0; dynamicStrings=0; collections=0; managedReentry=0; schedulerTransitions=0; arbitraryStackScans=0; largeDumps=0; safeStop='bounded scalar/pointer diagnostics; stopped before any native continuation attempt' }
            qemu=[ordered]@{ version=$qemuVersion; runCount=$FreshBootCount; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); exactCommandLog=(Join-Path $runRoot 'commands.txt'); evidenceRoot=$runRoot; runs=$runResults }
            payloadHashes=[ordered]@{ pe=(Hash-File $pePath); elf=(Hash-File $elfPath); proofKernel=$specializedKernelHash; map=(Hash-File $mapPath) }
            regressions=[ordered]@{ C011EC20='PASS retained: one genuine managed unwind into native RIP 0x1AE365; no reinterpretation'; C011EC19='PASS retained: four category-3 roots, six total roots, four stack Promote attempts/entries/returns'; C011EC15='PASS retained: queue chronology 4 -> 5, mark writes 0, child reads 0, graph traversal 0'; converter='PASS PE-to-ELF converter invocation and fixed-base map validation'; PowerShell='PASS parser validation'; sourceGuards='PASS locked-source and manifest checks'; ordinaryPayload='PASS restored by finally'; staticChecks='PASS git diff --check, serial checkpoints, native symbol/disassembly/unwind audit' }
            retainedChronology=[ordered]@{ totalRoots='6'; category3Roots='4'; registerRoots='3'; stackRoots='1'; promoteAttempts='4'; promoteEntries='4'; promoteReturns='4'; firstStackDerivedSlot='retained historical C011EC19 value'; queueCursor='4 -> 5'; markWrites='0'; childReads='0'; graphTraversal='0' }
            documentation='docs/dotnet/NATIVEAOT_WORKSTATION_GC_NATIVE_TRANSITION_CONTINUATION.md'; evidenceRoot=$runRoot; manifestPath=$manifestPath; ordinaryRestoration=[ordered]@{ expectedBuildSha256=$normalKernelHash; expectedEspSha256=$normalKernelHash; restoredByFinally=$true }
        }
        $manifest | ConvertTo-Json -Depth 40 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT native transition continuation experiment: Outcome E / C011EC21" -ForegroundColor Green
    } elseif ($isC011EC20) {
        if (@($runResults).Count -ne $FreshBootCount -or @($runResults | Where-Object { $_.safeStopMarker -notin @('C011EC20','C011EC20-SAFE_STOP') }).Count -ne 0) { throw "The C011EC20 caller-frame experiment did not produce $FreshBootCount classified unwind runs." }
        $firstC20Run = $runResults[0]
        $c20Outcome = if (@($runResults | Where-Object { $_.outcome -ne $firstC20Run.outcome }).Count -eq 0) { $firstC20Run.outcome } else { 'D' }
        $manifest = [ordered]@{
            outcome="$c20Outcome / C011EC20 first ordinary unwind classification"; proofMode=$ProofMode; marker=if ($c20Outcome -in @('A','B','E')) { 'C011EC20' } else { 'C011EC20-SAFE_STOP' }; preflightMarker='C011EC20-PREFLIGHT'; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            lockedRuntimeIdentity=[ordered]@{ nativeAot='9.0.0'; architecture='AMD64'; gc='Workstation'; gcInterfaces='5.3 / 2'; sourceCommit=$lockedCommit; codeManager='CoffNativeCodeManager' }
            ordinaryBaseline=[ordered]@{ startingBuildSha256=$ordinaryKernelBefore.build; startingEspSha256=$ordinaryKernelBefore.esp; expectedSha256=$normalKernelHash }
            transition=$firstC20Run.transition; unwind=$firstC20Run.unwind; caller=$firstC20Run.caller; roots=$firstC20Run.roots; accounting=$firstC20Run.accounting; runs=$runResults
            sourceTrace=[ordered]@{ transitionDecision='locked CoffNativeCodeManager.cpp:671-707'; ordinaryUnwind='locked CoffNativeCodeManager.cpp:735-842'; iteratorContinuation='locked StackFrameIterator.cpp:1529-1600'; callerLookup='locked CoffNativeCodeManager.cpp:254-300 plus C011EC20 independent validation' }
            regressions=[ordered]@{ C011EC19='retained source path and root chronology'; C011EC18='retained transition provenance'; C011EC15='retained queue/promotion counters'; staticChecks='script parse, source-injection guards, serial checkpoints, ordinary restoration, git diff --check' }
            documentation='docs/dotnet/NATIVEAOT_WORKSTATION_GC_CALLER_FRAME_UNWIND.md'; ordinaryRestoration=[ordered]@{ expectedBuildSha256=$normalKernelHash; expectedEspSha256=$normalKernelHash; restoredByFinally=$true }; sensitiveAllocations='0 observed after EE suspension; bounded scalar diagnostics only'
        }
        $manifest | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT caller-frame unwind experiment: Outcome $c20Outcome (C011EC20)" -ForegroundColor Green
    } elseif ($isC011EC19) {
        if (@($runResults).Count -ne $FreshBootCount -or @($runResults | Where-Object { $_.safeStopMarker -ne 'C011EC19' }).Count -ne 0) { throw "The C011EC19 unwind/GC-info experiment did not produce $FreshBootCount genuine metadata-boundary runs." }
        $firstC19Run = $runResults[0]
        $c19Outcome = if (@($runResults | Where-Object { $_.outcome -ne $firstC19Run.outcome }).Count -eq 0) { $firstC19Run.outcome } else { 'E' }
        if ($c19Outcome -eq 'E') { throw "C011EC19 semantic outcome was not deterministic across fresh QEMU boots." }
        $manifest = [ordered]@{
            outcome=if ($c19Outcome -eq 'A') { 'A / genuine CoffNativeCodeManager unwind metadata and genuine GC-info decoding were consumed; root callbacks completed and NativeAOT reached the reverse-PInvoke transition unwind boundary' } else { 'B / genuine caller-frame unwind metadata was consumed; GC-info decoding was the next blocker' }
            proofMode=$ProofMode; marker='C011EC19'; preflightMarker='C011EC19-PREFLIGHT'; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            lockedRuntimeIdentity=[ordered]@{ nativeAot='9.0.0'; architecture='AMD64'; gc='Workstation'; gcInterfaces='5.3 / 2'; sourceCommit=$lockedCommit; codeManager='CoffNativeCodeManager' }
            ordinaryBaseline=[ordered]@{ startingBuildSha256=$ordinaryKernelBefore.build; startingEspSha256=$ordinaryKernelBefore.esp; expectedSha256=$normalKernelHash }
            frame=$firstC19Run.frame; unwind=$firstC19Run.unwind; gcInfo=$firstC19Run.gcInfo; roots=$firstC19Run.roots; promotion=$firstC19Run.promotion; queue=$firstC19Run.queue; graph=$firstC19Run.graph; bounds=$firstC19Run.bounds; existingObjectGraph=$firstC19Run.existingObjectGraph; runtimeState=$firstC19Run.runtime; sensitiveAllocations='0 observed after EE suspension; proof path uses allocation-free scalar diagnostics'
            runs=$runResults
            payloadHashes=[ordered]@{ pe=(Hash-File $pePath); elf=(Hash-File $elfPath); proofKernel=$specializedKernelHash }
            qemu=[ordered]@{ version=$qemuVersion; runCount=$FreshBootCount; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); exactCommandLog=(Join-Path $runRoot 'commands.txt'); evidenceRoot=$runRoot }
            sourceTrace=[ordered]@{ findMethodInfo='locked CoffNativeCodeManager.cpp:254-300'; framePointer='locked CoffNativeCodeManager.cpp:323-337'; gcInfo='locked CoffNativeCodeManager.cpp:375-395, :434-496'; unwind='locked CoffNativeCodeManager.cpp:651-842'; rootEnumeration='locked GcEnum.cpp:68-96'; threadWalk='locked thread.cpp:442-569' }
            regressions=[ordered]@{ C011EC19="PASS $FreshBootCount/$FreshBootCount fresh QEMU 11.0.0 boots with deterministic Outcome $c19Outcome"; C011EC18='retained: authentic managed ControlPC, manager, FindMethodInfo, and frame-pointer boundary'; C011EC15='retained counters and ThreadStatic/storage-object queue provenance'; staticChecks='script parse, source-injection guards, serial checkpoints, ordinary restoration, git diff --check' }
            documentation='docs/dotnet/NATIVEAOT_WORKSTATION_GC_UNWIND_GC_INFO_BOUNDARY.md'
            ordinaryRestoration=[ordered]@{ expectedBuildSha256=$normalKernelHash; expectedEspSha256=$normalKernelHash; restoredByFinally=$true }
        }
        $manifest | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT unwind/GC-info boundary experiment: Outcome $c19Outcome (C011EC19)" -ForegroundColor Green
    } elseif ($isTransitionFrameControlPc) {
        if (@($runResults).Count -ne $FreshBootCount -or @($runResults | Where-Object { $_["safeStopMarker"] -ne "C011EC15" -or $_["outcome"] -ne "A" }).Count -ne 0) { throw "The C011EC18 transition-frame provenance experiment did not produce $FreshBootCount deterministic Outcome A runs." }
        $firstTransitionFrameRun = $runResults[0]
        $manifest = [ordered]@{
            outcome="A / the native wrapper provenance defect was repaired by restoring the locked NativeAOT RhpNewArray entry; the structurally saved PInvokeTransitionFrame m_RIP was presented to StackFrameIterator, CoffNativeCodeManager lookup succeeded, FindMethodInfo succeeded, and the real C011EC15 provider boundary was reached"
            proofMode=$ProofMode; marker="C011EC18"; preflightMarker="C011EC18-PREFLIGHT"; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            lockedRuntimeIdentity=[ordered]@{ nativeAot="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterfaces="5.3 / 2"; sourceCommit=$lockedCommit; codeManager="CoffNativeCodeManager" }
            ordinaryBaseline=[ordered]@{ startingBuildSha256=$ordinaryKernelBefore.build; startingEspSha256=$ordinaryKernelBefore.esp; expectedSha256=$normalKernelHash }
            transition=$firstTransitionFrameRun.transition; iterator=$firstTransitionFrameRun.iterator; methodInfo=$firstTransitionFrameRun.methodInfo; stack=$firstTransitionFrameRun.stack; queue=$firstTransitionFrameRun.queue; root=$firstTransitionFrameRun.root; invariants=$firstTransitionFrameRun.invariants; registration=$firstTransitionFrameRun.registration; c011ec18=$firstTransitionFrameRun.c011ec18
            runs=$runResults
            qemu=[ordered]@{ version=$qemuVersion; runCount=$FreshBootCount; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); exactCommandLog=(Join-Path $runRoot "commands.txt"); evidenceRoot=$runRoot }
            regressions=[ordered]@{ C011EC18="PASS $FreshBootCount/$FreshBootCount fresh QEMU runs with deterministic transition provenance and Outcome A"; C011EC17="historical registration result retained; the prior null-manager boundary was crossed naturally"; C011EC15="real next-provider safe-stop retained"; C011EC14_to_C011EC02="historical evidence retained; not relabeled"; staticChecks="script parse, manifest parse, serial checkpoints, ordinary restoration, git diff --check" }
            documentation="docs/dotnet/NATIVEAOT_WORKSTATION_GC_TRANSITION_FRAME_CONTROL_PC_PROVENANCE.md"
            ordinaryRestoration=[ordered]@{ expectedBuildSha256=$normalKernelHash; expectedEspSha256=$normalKernelHash; restoredByFinally=$true }
        }
        $manifest | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT transition-frame control-PC provenance experiment: Outcome A (C011EC18)" -ForegroundColor Green
    } elseif ($isCodeManagerRegistration) {
        if (@($runResults).Count -ne $FreshBootCount -or @($runResults | Where-Object { $_["outcome"] -notin @("A", "B", "D") }).Count -ne 0) { throw "The C011EC17 code-manager registration experiment did not produce $FreshBootCount valid runs." }
        $firstCodeManagerRun = $runResults[0]
        $outcome = if ((@($runResults | Where-Object { $_["outcome"] -ne $firstCodeManagerRun.outcome }).Count -eq 0)) { $firstCodeManagerRun.outcome } else { "E" }
        if ($outcome -eq "E") { throw "C011EC17 semantic outcome was not deterministic across the fresh QEMU runs." }
        $manifest = [ordered]@{
            outcome=if ($outcome -eq "A") { "A / production NativeAOT code-manager registration and managed-range lookup succeeded; the authentic stack walker reached the existing C011EC15 provider boundary" } elseif ($outcome -eq "B") { "B / production NativeAOT code-manager registration and managed-range lookup succeeded; the next post-lookup method/unwind metadata boundary failed" } else { "D / production code-manager registration succeeded, but the authentic C011EC16 control PC was outside the valid NativeAOT managed-code bookend range, so the null lookup boundary remained and no success marker was emitted" }
            proofMarker=if ($outcome -eq "A") { "C011EC17" } else { "none" }; preflightMarker="C011EC17-PREFLIGHT"; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            lockedRuntimeIdentity=[ordered]@{ nativeAot="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterfaces="5.3 / 2"; sourceCommit=$lockedCommit; codeManager="CoffNativeCodeManager" }
            ordinaryBaseline=[ordered]@{ startingBuildSha256=$ordinaryKernelBefore.build; startingEspSha256=$ordinaryKernelBefore.esp; expectedSha256=$normalKernelHash }
            registration=$firstCodeManagerRun.registration
            methodInfo=$firstCodeManagerRun.methodInfo
            stack=$firstCodeManagerRun.stack
            runs=$runResults
            qemu=[ordered]@{ version=$qemuVersion; runCount=$FreshBootCount; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); debugLogs=@($runResults | ForEach-Object { $_.debugLog }); exactCommandLog=(Join-Path $runRoot "commands.txt"); evidenceRoot=$runRoot }
            regressions=[ordered]@{ C011EC17="PASS $FreshBootCount/$FreshBootCount fresh QEMU 11.0.0 runs with deterministic semantic outcome $outcome"; C011EC16="Outcome D retained as historical documentation; prior null-manager boundary was not rewritten"; C011EC15="retained and observed by the existing focused root instrumentation"; C011EC14_to_C011EC02="historical evidence retained; not relabeled"; staticChecks="script parse, manifest parse, serial checkpoints, ordinary restoration, git diff --check" }
            documentation="docs/dotnet/NATIVEAOT_WORKSTATION_GC_CODE_MANAGER_REGISTRATION.md"
            ordinaryRestoration=[ordered]@{ expectedBuildSha256=$normalKernelHash; expectedEspSha256=$normalKernelHash; restoredByFinally=$true }
        }
        $manifest | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT production code-manager registration experiment: Outcome $outcome" -ForegroundColor Green
    } elseif ($isStackProviderTransitionFailFast) {
        if (@($runResults).Count -ne $FreshBootCount -or @($runResults | Where-Object { $_["outcome"] -ne "D" }).Count -ne 0) { throw "The stack-provider transition fail-fast experiment did not produce $FreshBootCount deterministic Outcome D runs." }
        $firstTransitionRun = $runResults[0]
        $transitionSerial = Get-Content -LiteralPath $firstTransitionRun.serial -Raw
        $transitionLine = (($transitionSerial -split "`n") | Where-Object { $_ -match 'GcScanRoots-entry sentinel=' } | Select-Object -Last 1)
        $transitionStorageObject = Get-MarkerField $transitionSerial 'storageObject'
        $failFastLine = (($transitionSerial -split "`n") | Where-Object { $_ -match '\[nativeaot-pal-qemu-test\] FAIL_FAST reason=47435354' } | Select-Object -Last 1)
        $manifest = [ordered]@{
            outcome="D / Thread::GcScanRoots entry reached; StackFrameIterator prologue failed before first stack-provider callback"; proofMode=$ProofMode; marker="none (C011EC16 not reached)"; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            ordinaryStartingHashes=[ordered]@{ build=$ordinaryKernelBefore.build; esp=$ordinaryKernelBefore.esp; expected=$normalKernelHash }; lockedRuntimeIdentity=[ordered]@{ nativeAot="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterfaces="5.3 / 2"; sourceCommit=$lockedCommit }
            prerequisite=[ordered]@{ marker="C011EC15"; historicalOutcome="E retained"; correctedFinding="category 3 and Thread::GcScanRoots entry reached" }; firstRoot=[ordered]@{ field="[ThreadStatic] byte[]? s_gcProofThreadRoot"; sentinel=(Get-MarkerField $transitionLine 'sentinel'); storageObject=$transitionStorageObject; priorFullProof="0x100A01F38 / 0x100A02F50" }
            ordinaryThreadStaticProvider=[ordered]@{ entered=$true; returned="null"; callbackReturns=1; rootSlotsVisited=2; nullCandidates=1; nonNullCandidates=1; secondPromoteAttempts=0; secondQueueInsertions=0 }; queue=[ordered]@{ slotIndex=0; cursor="0 -> 1"; old="null"; new=$transitionStorageObject; drain=0; markBitWrites=0; childTraversal=0; preserved=$true }
            sourceTrace=[ordered]@{ gcScanRoots="locked nativeaot/Runtime/gcenv.ee.cpp:94-133; generated gcenv.ee.single-thread-suspend-ee.cpp:123-181"; ordinaryProvider="gcenv.ee.cpp:114-115"; categoryTransition="generated gcenv.ee.single-thread-suspend-ee.cpp:160-165"; threadUnderCrawl="generated gcenv.ee.single-thread-suspend-ee.cpp:165"; threadGcScanRoots="locked nativeaot/Runtime/thread.cpp:393-403"; iterator="locked nativeaot/Runtime/StackFrameIterator.cpp:1913-1935"; codeManagerLookup="locked nativeaot/Runtime/RuntimeInstance.cpp:101-109" }
            failFast=[ordered]@{ code="0x47435354"; bytesLittleEndian="54 53 43 47"; asciiMemoryOrder="TSCG"; msbToLsbText="GCST"; meaning="guideXOS startup-probe tag for shimmed RaiseFailFastException/abort/assert paths; not an official NativeAOT tag"; source="guidexos_nativeaot_gc_startup_probe.cpp:87; rhassert.h:63-65"; helperChain="CalculateCurrentMethodState -> RhFailFast -> RaiseFailFastException -> startupFailFast -> guidexos_nativeaot_pal_fail_fast" }
            machineCode=[ordered]@{ ordinaryEnumGcRefCall="0x10013b66 -> 0x100144b0"; category3Call="0x10013b76 -> 0x10006fd0"; threadUnderCrawlStore="0x10013ba0"; threadGcScanRootsCall="0x10013ba9 -> 0x10009910"; threadGcScanRootsEntry="0x10009910"; calculateCurrentMethodState="0x1000d730"; getCodeManager="0x1000d7a8 -> 0x1000abe0"; branch="0x1000d7b4 test rax,rax; 0x1000d7b7 jne 0x1000d7c8 not taken"; failFastCall="0x1000d7c3 call 0x10007f40; return 0x1000d7c8"; startupTagLoad="0x10007fb4 mov ecx,0x47435354"; immediateCaller="StackFrameIterator::CalculateCurrentMethodState"; lastHelper="RuntimeInstance::GetCodeManagerForAddress"; controlPC="0x10004d66"; codeManagerResult="NULL"; callOperands="RCX=0 RDX=0 R8=1"; gdbEvidence=(Join-Path $runRoot 'live-gdb-final4/live-gdb-final4.gdb-output.txt') }
            runtimeState=[ordered]@{ currentThread="0x393DC00"; enumeratedThread="0x393DC00"; collectionInitiator="0x393DC00"; threadStoreLockOwner="0x393DC00"; lockHeld=1; recursionDepth=1; threadStateFlags="0x1"; cooperative=1; preemptive=0; nativeThreadId="0x100D88D0"; stackBase="0x0"; stackLimit="0x0"; currentRSP="0x4E79088"; transitionFrameRIP="0x10004D66"; scanContext="0x4E79440"; threadUnderCrawl="0x393DC00"; scanContextStackLimit="0x0"; threadNumber=0; threadCount=1; promotion=1; concurrent=0 }
            allocationAudit=[ordered]@{ allocationsAttemptedAfterSuspension=0; proofDiagnosticAllocations=0; NativeAotAllocations=0; threadStaticAllocations=0; temporaryRuntimeAllocations=0; managedEntryAttempts=0; NativeAotHelpersInTransition=0; evidence="scalar counters/direct serial I/O only" }; instrumentationMinimization=[ordered]@{ change="suppressed optional diagnostic lines and retained only transition markers/counters"; allocationFree=$true; before="full C011EC15 output; 0x47435354"; after="minimal markers; 0x47435354"; behaviorChanged=$false; productionRuntimeChanged=$false }
            stackProvider=[ordered]@{ threadGcScanRootsCalls=1; functionEntry=1; prologueFailed=$true; framesWalked=0; stackRootSlotsVisited=0; stackNonNullCandidates=0; stackPromoteCalls=0; callbackCount=0; gcInfoRead=0; rootMapRead=0; stackDescriptorAccessed=0 }; invariants=[ordered]@{ eeSuspended=1; managedEntryProhibited=1; restart=0; resume=0; queueSlot0Preserved=1; queueCursorAtStop=1; markBitWrites=0; childReferenceReads=0; graphTraversal=0; objectMutation=0; sentinelUnchanged=1; storageObjectUnchanged=1 }
            outcomeClassification=[ordered]@{ result="D"; violatedPrecondition="GetCodeManagerForAddress(m_ControlPC) returned null; RuntimeInstance m_CodeManager and managed range were zero"; classification="real production stack/unwind/code-manager invariant failure inside Thread::GcScanRoots prologue, not proof instrumentation"; fixMade=$false; C011EC16="not reached" }; qemu=[ordered]@{ version=$qemuVersion; runCount=$FreshBootCount; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); exactCommandLog=(Join-Path $runRoot 'commands.txt'); runs=$runResults; failFastLine=$failFastLine }
            regressions=[ordered]@{ failureIsolation="PASS 3/3 fresh QEMU Outcome D"; C011EC15="historical E retained"; C011EC14="historical D retained"; C011EC13="historical B retained"; C011EC12="historical D retained"; C011EC11="historical A retained"; C011EC10="historical A retained"; C011EC09="retained non-clean"; broaderSuite="historical evidence retained; focused pass only" }; historicalEvidence=[ordered]@{ C011EC15="retained"; historical64KiB="retained"; staleCache="retained"; runtimePackIdentityMismatch="retained"; nativeStackWrapper="retained non-clean"; localStorageTeardown="retained"; raceSerialWrap="retained" }
            documentation="docs/dotnet/NATIVEAOT_WORKSTATION_GC_STACK_PROVIDER_TRANSITION_FAILFAST.md"; evidenceRoot=$runRoot; manifestPath=$manifestPath; ordinaryRestoration=[ordered]@{ expectedBuildSha256=$normalKernelHash; expectedEspSha256=$normalKernelHash; restoredByFinally=$true }
        }
        $manifest | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT stack-provider transition fail-fast experiment: Outcome D (diagnostic boundary)" -ForegroundColor Yellow
    } elseif ($isNextGenuineRootProvider) {
        if (@($runResults).Count -ne $FreshBootCount -or @($runResults | Where-Object { $_["safeStopMarker"] -ne "C011EC15" }).Count -ne 0) { throw "The C011EC15 experiment did not produce $FreshBootCount C011EC15 runs." }
        $firstC15Run = $runResults[0]
        $manifest = [ordered]@{
            outcome="A / the first genuine inline thread-static root completed its bounded Workstation queue_mark path and root enumeration reached the next genuine non-null candidate before its second Promote"
            proofMode=$ProofMode; marker="C011EC15"; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            lockedRuntimeIdentity=[ordered]@{ nativeAot="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterfaces="5.3 / 2"; sourceCommit=$lockedCommit; generatedGcenv=$gcEnvEeSource; generatedGcEnum=$gcEnumSource; generatedGcwks=$gcWksSource }
            ordinaryBaseline=[ordered]@{ startingBuildSha256=$ordinaryKernelBefore.build; startingEspSha256=$ordinaryKernelBefore.esp; expectedSha256=$normalKernelHash }
            prerequisite=[ordered]@{ marker="C011EC14"; outcome="D retained"; firstQueueInsertions=1; firstQueueSlot=0; firstQueueCursor="0 -> 1"; oldObject="null"; queueSeeding=$false }
            firstRoot=$firstC15Run.root; nextGenuineRoot=$firstC15Run.nextRoot; accounting=$firstC15Run.accounting; queue=$firstC15Run.queue; prohibited=$firstC15Run.prohibited; threadStore=$firstC15Run.threadStore
            providerOrder=[ordered]@{ lockedSource="inline thread-static roots -> ordinary thread-static storage -> Thread::GcScanRoots stack/register/root-map/exception/GC-frame roots; finalizer roots -> handles -> dependent handles later in GCHeap::GarbageCollectGeneration"; first=$firstC15Run.providerOrder.first; next=$firstC15Run.providerOrder.next; stackRootEntered=($firstC15Run.nextRoot.providerCategory -eq "0x00000003"); staticModuleEntered=$false; handlesEntered=$false; finalizerEntered=$false }
            callback=[ordered]@{ firstRootReturnCount=$firstC15Run.accounting.firstRootCallbackReturns; enumGcRefContinuations=$firstC15Run.accounting.enumGcRefContinuations; promoteReturns=$firstC15Run.accounting.promoteReturns; markHelperReturns=$firstC15Run.accounting.markHelperReturns; secondPromoteAttempts="0x00000000"; secondPromoteEntries="0x00000000" }
            provenance=[ordered]@{ firstRoot="real NativeAOT inline thread-static storage object"; secondRoot="real Object** observed through locked NativeAOT EnumGcRef continuation before callback"; genuineManagedObject=$true; graphTraversal=0; childReferenceReads=0; stackMapDecoder="not separately entered before the provider boundary" }
            sourceTrace=[ordered]@{ gcScanRoots="locked nativeaot/Runtime/gcenv.ee.cpp:94-133"; enumGcRef="locked nativeaot/Runtime/GcEnum.cpp:68-96 and :84-96"; threadRoots="locked nativeaot/Runtime/thread.cpp:393-403, :442-569"; queue="locked gc.cpp:27303-27335"; promote="locked gc.cpp:49474-49544"; gcPhase="locked gc.cpp:29897-29926" }
            qemu=[ordered]@{ version=$qemuVersion; runCount=$FreshBootCount; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); exactCommandLog=(Join-Path $runRoot "commands.txt"); runs=$runResults }
            regressions=[ordered]@{ c011ec15="PASS $FreshBootCount/$FreshBootCount fresh QEMU runs"; c011ec14="Outcome D retained"; c011ec13_to_c011ec02="historical checkpoints retained"; c011ec09="historical non-clean interpretation retained"; threadStaticVariants="historical primitive/reference/combined evidence retained"; broadSuite="not rerun as one combined pass"; staticChecks="script parsing, manifest parsing, serial evidence, exact kernel build, ordinary restoration, git diff --check" }
            retainedFailures=[ordered]@{ historicalFirst64KiBExecution="retained"; staleCacheAttempts="retained"; initialRuntimePackIdentityMismatch="retained"; nativeStackPowerShellWrapper="retained NON-CLEAN"; localStorageTeardown="retained"; historicalRaceSerialWrap="retained" }
            blockedNonClean=[ordered]@{ broadRegressionSuite="not rerun in one combined pass"; stackSubkind="provider boundary reached; individual stack-map subkind not crossed" }
            documentation=@("docs/dotnet/NATIVEAOT_WORKSTATION_GC_NEXT_GENUINE_ROOT_PROVIDER.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_DISPLACED_MARK_READ.md")
            evidenceRoot=$runRoot; reportPath="docs/dotnet/NATIVEAOT_WORKSTATION_GC_NEXT_GENUINE_ROOT_PROVIDER.md"; manifestPath=$manifestPath; ordinaryRestoration=[ordered]@{ expectedBuildSha256=$normalKernelHash; expectedEspSha256=$normalKernelHash; restoredByFinally=$true }
            authorizedNormalizedAdaptedGcIdentity=[ordered]@{ sourceCommit=$lockedCommit; activeArchiveSha256=(Hash-File $activeArchive); stockUnchanged=$true }
        }
        $manifest | ConvertTo-Json -Depth 28 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT Workstation GC next-genuine-root-provider experiment: PASS (Outcome A)" -ForegroundColor Green
    } elseif ($isFirstRootCallbackEntry) {
        $nonC011EC07Runs = @($runResults | Where-Object { $_["safeStopMarker"] -ne "C011EC07" })
        if (@($runResults).Count -ne 3 -or $nonC011EC07Runs.Count -ne 0) { throw "The first real root callback-entry experiment did not produce three C011EC07 runs." }
        $firstCallbackRun = $runResults[0]
        $manifest = [ordered]@{
            outcome="A / real GCHeap::Promote callback entered exactly once with the genuine NativeAOT thread-static storage root and stopped before heap classification"
            proofMode=$ProofMode; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            taskStartCheckpoint=$taskStartCheckpoint
            productionStartup=[ordered]@{ initializeModules="production runtime path invoked before ManagedMain"; status="working" }
            ordinaryBaseline=[ordered]@{ startingBuildSha256=$ordinaryBuildBefore; startingEspSha256=$ordinaryEspBefore; expectedSha256=$normalKernelHash }
            runtimePack=[ordered]@{ version="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterface="5.3"; ee="2"; sourceCommit=$lockedCommit; lockedGcenvEeSourceSha256=(Hash-File $lockedEePath); lockedGcEnumSourceSha256=(Hash-File $lockedGcEnumPath); lockedGcCppSourceSha256=(Hash-File $lockedGcCppPath); lockedGcwksSourceSha256=(Hash-File $lockedGcwksPath); activePalArchiveSha256=(Hash-File $activeArchive); callbackSymbol=$callbackSymbolName; callbackAddress="0x$callbackAddressText" }
            priorCheckpoint=[ordered]@{ marker="C011EC06"; report="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_ROOT_CALLBACK_BOUNDARY.md"; stop="one genuine non-null NativeAOT thread-static storage root discovered before callback invocation" }
            managedProofRoot=$firstCallbackRun.managedProofRoot
            threadStaticRuntime=[ordered]@{ storageObjectAddress=$firstCallbackRun.managedProofRoot.storageObject; inlinedRootAddress=$firstCallbackRun.managedProofRoot.inlinedRoot; normalManagedSemantics=$true; fabricatedSlot=$false; fabricatedObject=$false }
            sourceTrace=[ordered]@{ callbackTypedef="promote_func(Object**, ScanContext*, uint32_t)"; callbackFunction="GCHeap::Promote"; callbackSource="locked src/coreclr/gc/gc.cpp:49474-49544, included by src/coreclr/gc/gcwks.cpp"; callbackGeneration="locked src/coreclr/gc/gc.cpp:29899-29901: GCScan::GcScanRoots(GCHeap::Promote, ...)"; rootProvider="locked src/coreclr/nativeaot/Runtime/gcenv.ee.cpp:114-115"; enumGcRef="locked src/coreclr/nativeaot/Runtime/GcEnum.cpp:68-96"; callbackCallSite="locked GcEnum.cpp:81: fnGcEnumRef(ppObj, pSc, flags)"; firstSourceStatement="locked gc.cpp:49476 THREAD_NUMBER_FROM_CONTEXT"; firstCandidateLoad="locked gc.cpp:49481: uint8_t* o = (uint8_t*)*ppObject"; firstHeapClassification="locked gc.cpp:49483: gc_heap::is_in_find_object_range(o)"; firstMetadataAccess="locked gc.cpp:49496: gc_heap::heap_of(o) and subsequent generation/range checks"; firstPromotionMutation="locked gc.cpp:49533 pin_object / gc_heap::mark_object_simple at :49541"; generatedGcwks=$gcWksSource; generatedGcEnum=$gcEnumSource }
            abi=[ordered]@{ architecture="AMD64"; callingConvention="Microsoft x64"; argumentRegisters=@("RCX=Object** root slot", "RDX=ScanContext*", "R8D/R8=root flags uint32_t"); returnRegister="RAX unused for void"; shadowSpace="32 bytes required by caller"; callSiteDisassembly=(Join-Path $runRoot "artifact-disassembly.txt"); callbackEntryDisassembly=(Join-Path $runRoot "artifact-disassembly.txt"); callbackSymbolFile=(Join-Path $runRoot "callback-symbol.txt") }
            callbackCallSite=$firstCallbackRun.callbackSite
            callbackEntry=$firstCallbackRun.callbackEntry
            rootArgument=$firstCallbackRun.root
            scanContext=$firstCallbackRun.scanContext
            flags=$firstCallbackRun.flags
            expectedArgumentMatches=[ordered]@{ argument1EqualsRealInlineRootSlot=($firstCallbackRun.callbackEntry.arg1 -eq $firstCallbackRun.root.firstNonNullSlot); argument1SlotLoadEqualsStorageObject=($firstCallbackRun.root.loadedValue -eq $firstCallbackRun.root.expectedStorageObject); argument2EqualsLiveProviderScanContext=($firstCallbackRun.callbackEntry.arg2 -eq $firstCallbackRun.callbackSite.scanContext); historicalKnownScanContext="0x0000000004E694E0 (prior checkpoint absolute; stack address is run-specific)"; argument3MatchesExpectedRootFlags=([Convert]::ToUInt64($firstCallbackRun.callbackEntry.arg3.Substring(2),16) -eq [Convert]::ToUInt64($firstCallbackRun.flags.expected.Substring(2),16)); callbackPointerMatchesLockedMap=("0x" + $callbackAddressText.ToUpperInvariant() -eq $firstCallbackRun.callbackSite.callback) }
            callbackCounts=[ordered]@{ requests=$firstCallbackRun.callbackRequestCount; callSiteEntries=$firstCallbackRun.callbackCallSiteCount; invocations=$firstCallbackRun.callbackInvocationCount; entries=$firstCallbackRun.callbackEntryCount; returns=$firstCallbackRun.callbackReturnCount; duplicates=$firstCallbackRun.duplicateCallbackInvocations }
            callbackSideRootLoad=[ordered]@{ count=$firstCallbackRun.root.callbackSideLoadCount; value=$firstCallbackRun.root.loadedValue; nullTests=(Get-MarkerField (Get-Content -LiteralPath $firstCallbackRun.serial -Raw) 'nullTests'); result="non-null storage object; one locked callback-side load" }
            semanticBoundary=$firstCallbackRun.semanticBoundary
            mutation=$firstCallbackRun.mutation
            objectAndSentinelValidation=$firstCallbackRun.managedProofRoot
            threadStore=$firstCallbackRun.threadStore
            safeStop=[ordered]@{ marker="C011EC07"; reason="real GCHeap::Promote entered with validated live ABI arguments, performed its source-required *ppObject load, then stopped immediately before gc_heap::is_in_find_object_range"; firstProhibitedOperation="candidate heap-membership classification at locked gc.cpp:49483"; promotionAllowed=$false; markingAllowed=$false; graphTraversalAllowed=$false; callbackReturnAllowed=$false }
            qemu=[ordered]@{ version=$qemuVersion; runCount=3; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); exactCommandLog=(Join-Path $runRoot "commands.txt"); runs=$runResults }
            regressions=[ordered]@{ firstRootCallbackEntry="PASS 3/3 fresh QEMU runs"; firstNonNullRootCallbackBoundary="C011EC06 prerequisite retained"; firstRootCandidateLoad="C011EC05 prerequisite retained"; threadStaticPrimitive="historical proof retained"; threadStaticReference="historical proof retained"; threadStaticCombined="historical proof retained"; firstPerThreadRootProvider="C011EC04 prerequisite retained"; allocationContextFixupRootBoundary="C011EC03 prerequisite retained"; singleThreadSuspendEe="C011EC02 prerequisite retained"; staticChecks="PASS script parse, manifest parse, serial classification, ordinary-kernel restoration, git diff --check" }
            failedChecks=[ordered]@{ historicalFirst64KiBExecution="retained historical failure"; staleCacheAttempts="retained historical attempts"; initialRuntimePackIdentityMismatch="retained historical mismatch"; nativeStackPowerShellWrapper="retained NON-CLEAN exit 1 from compiler-stderr promotion" }
            blockedChecks=[ordered]@{ broadRegressionSuite="not rerun in this focused execution"; nativeStackWrapper="historically non-clean; not called passed"; callbackPromotionCompletion="intentionally out of scope; proof stops before classification/promotion" }
            documentation=@("docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CALLBACK_ENTRY.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_ROOT_CALLBACK_BOUNDARY.md","docs/dotnet/NATIVEAOT_THREAD_STATIC_RUNTIME_SUPPORT.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CANDIDATE_LOAD.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_PER_THREAD_ROOT_PROVIDER.md","docs/dotnet/NATIVEAOT_GC_STARTUP_READINESS.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FEASIBILITY.md"); evidenceRoot=$runRoot
            ordinaryRestoration=[ordered]@{ buildSha256=$normalKernelHash; espSha256=$normalKernelHash; expectedSha256=$normalKernelHash }
            authorizedNormalizedAdaptedGcIdentity=[ordered]@{ strategy=$identity.strategy; sourceCommit=$identity.sourceCommit; stockSha256=$identity.stockSha256; adaptedSha256=$identity.adaptedSha256; adaptedLength=$identity.adaptedLength; replacementObjects=$replacementHashes; stockUnchanged=$identity.stockUnchanged }
        }
        $manifest | ConvertTo-Json -Depth 24 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT Workstation GC first-root-callback-entry experiment: PASS (Outcome A)" -ForegroundColor Green
    } elseif ($isFirstNonNullRoot) {
        $nonC011EC06Runs = @($runResults | Where-Object { $_["safeStopMarker"] -ne "C011EC06" })
        if (@($runResults).Count -ne 3 -or $nonC011EC06Runs.Count -ne 0) { throw "The first non-null root callback-boundary experiment did not produce three C011EC06 runs." }
        $firstEarlyRun = $runResults[0]
        $firstEarlyRun | Add-Member -NotePropertyName faultRip -NotePropertyValue $null -Force
        $firstEarlyRun | Add-Member -NotePropertyName faultCr2 -NotePropertyValue $null -Force
        $manifest = [ordered]@{
            outcome="E / NativeAOT real [ThreadStatic] initialization faulted before managed assignment completed; root-provider candidate scan not reached"
            proofMode=$ProofMode; repositoryHead=$repoHead; startingCommittedHead=$repoHead; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            runtimePack=[ordered]@{ version="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterface="5.3"; ee="2"; sourceCommit=$lockedCommit; lockedGcenvEeSourceSha256=(Hash-File $lockedEePath); lockedGcEnumSourceSha256=(Hash-File $lockedGcEnumPath); generatedInjectedGcenvEeSource=$gcEnvEeSource; generatedInjectedGcEnumSource=$gcEnumSource; activePalArchiveSha256=(Hash-File $activeArchive) }
            priorCheckpoint=[ordered]@{ marker="C011EC05"; report="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CANDIDATE_LOAD.md"; stop="one real candidate-slot machine-word load, before callback and semantic processing" }
            managedProofRoot=[ordered]@{ field="HostLogProof.Program.s_gcProofThreadRoot"; attribute="[ThreadStatic]"; assignmentCount="0x00000000"; readbackCount="0x00000000"; assignmentValid="0x00000000"; readbackValid="0x00000000"; initializationIndicator="0x00000000"; selectedSentinel="not assigned before fault"; fabricatedSlot=$false; fabricatedObject=$false }
            sourceTrace=[ordered]@{ managedSemantics="NativeAOT normal ThreadStatic slow path"; threadStaticBase="nativeaot/Runtime/thread.cpp:1251-1261"; inlineThreadStaticRoot="nativeaot/Runtime/thread.h:76-84"; rootDispatcher="nativeaot/Runtime/gcenv.ee.cpp:94-133"; enumGcRef="nativeaot/Runtime/GcEnum.cpp:68-96"; injectedCandidateLoad=$gcEnumSource; faultFunction="S_P_CoreLib_Internal_Runtime_ThreadStatics__GetInlinedThreadStaticBaseSlow" }
            failure=[ordered]@{ classification="nativeaot-thread-static-startup-page-fault"; firstManagedEntry=$true; serialEvidence="[PageFault] Not-present violation on read (kernel) at 0xFFFB5FF9"; faultRip=$firstEarlyRun.faultRip; faultCr2=$firstEarlyRun.faultCr2; faultFunction="S_P_CoreLib_Internal_Runtime_ThreadStatics__GetInlinedThreadStaticBaseSlow"; safeStopMarker="not reached"; rootProviderEntered=$false; candidateLoadRequested=0; candidateMachineWordLoaded=0; callbacks=0; promotions=0; marking=0; objectMutation=0; restartRequests=0; restartEntries=0; managedResume=0; interpretation="normal NativeAOT thread-static initialization failed before the proof field could be assigned; this is not a fabricated runtime root or candidate result" }
            candidateBoundary=[ordered]@{ providerSource="not reached"; candidateVisited=0; nullCandidates=0; nonNullCandidates=0; firstNonNullValue="not observed"; exactSelectedSentinelMatch="not observed"; callback=0; scanContext="not observed"; candidateDereferences=0; heapMembershipTests=0; objectHeaders=0; methodTables=0; rootFlagApplications=0; safeStopMarker="C011EC06 not reached" }
            sentinelAndObjects=[ordered]@{ workloadSentinels=4; managedSentinelAssignment=$false; objectValidationAtManagedEntry="not applicable before assignment"; fabricatedSentinel=$false; fabricatedCandidate=$false }
            proofKernelSha256=$specializedKernelHash; qemuVersion=$qemuVersion; exactCommandLog=(Join-Path $runRoot "commands.txt"); logsRoot=$runRoot; runs=$runResults
            regressions=[ordered]@{ firstNonNullRootCallbackBoundary="OUTCOME E 3/3 fresh QEMU runs; same early NativeAOT thread-static page-fault class"; firstRootCandidateLoad="historical PASS C011EC05 retained; not overwritten"; firstPerThreadRootProvider="historical PASS C011EC04 retained"; allocationContextFixupRootBoundary="historical PASS C011EC03 retained"; singleThreadSuspendEe="historical PASS C011EC02 retained"; staticChecks="PASS script parse, manifest parse, QEMU serial classification, ordinary-kernel restoration" }
            failedChecks=[ordered]@{ historicalFirst64KiBExecution="retained historical failure"; staleCacheAttempts="retained historical attempts"; initialRuntimePackIdentityMismatch="retained historical mismatch"; nativeStackPowerShellWrapper="retained NON-CLEAN exit 1 from compiler-stderr promotion" }
            blockedChecks=[ordered]@{ firstNonNullRootCallbackBoundary="blocked at legitimate managed ThreadStatic initialization before root-provider callback boundary"; candidateAbiCharacterization="not reached because no real candidate was loaded"; broadRegressionSuite="not rerun in this focused execution"; nativeStackWrapper="historically non-clean; not called passed" }
            documentation=@("docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_ROOT_CALLBACK_BOUNDARY.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CANDIDATE_LOAD.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_PER_THREAD_ROOT_PROVIDER.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_ALLOCATION_CONTEXT_FIXUP_AND_FIRST_ROOT_BOUNDARY.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_SINGLE_THREAD_SUSPEND_EE.md"); evidenceRoot=$runRoot
            ordinaryKernelBefore=$ordinaryKernelBefore; ordinaryKernelExpectedSha256=$normalKernelHash; authorizedNormalizedAdaptedGcIdentity=[ordered]@{ strategy=$identity.strategy; sourceCommit=$identity.sourceCommit; stockSha256=$identity.stockSha256; adaptedSha256=$identity.adaptedSha256; adaptedLength=$identity.adaptedLength; replacementObjects=$replacementHashes; stockUnchanged=$identity.stockUnchanged }
        }
        $firstRun = $runResults[0]
        $proofRootMatch = $firstRun.candidateMatchesProofRoot -eq "0x00000001"
        $storageRootMatch = $firstRun.candidateMatchesStorageObject -eq "0x00000001"
        $outcome = if ($proofRootMatch -or $storageRootMatch) { "A / genuine managed [ThreadStatic] storage root reached the first non-null callback boundary" } else { "C / first genuine non-null candidate did not match the selected sentinel or storage object" }
        $manifest = [ordered]@{
            outcome=$outcome; proofMode=$ProofMode; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            productionStartup=[ordered]@{ initializeModules="production runtime path invoked before ManagedMain"; status="working" }
            runtimePack=[ordered]@{ version="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterface="5.3"; ee="2"; sourceCommit=$lockedCommit; lockedGcenvEeSourceSha256=(Hash-File $lockedEePath); lockedGcEnumSourceSha256=(Hash-File $lockedGcEnumPath); activePalArchiveSha256=(Hash-File $activeArchive) }
            priorCheckpoint=[ordered]@{ marker="C011EC05"; report="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CANDIDATE_LOAD.md"; stop="one real candidate-slot machine-word load, before callback and semantic processing" }
            managedProofRoot=$firstRun.managedProofRoot
            threadStaticRuntime=[ordered]@{ storageAllocationCount=$firstRun.runtimeThreadStaticStorageAllocations; storagePublicationCount=$firstRun.runtimeThreadStaticStoragePublications; storageObjectAddress=$firstRun.runtimeThreadStaticStorageObject; inlinedRootAddress=$firstRun.runtimeThreadStaticInlinedRoot; normalManagedSemantics=$true; fabricatedSlot=$false; fabricatedObject=$false }
            sourceTrace=[ordered]@{ threadStatics="System.Private.CoreLib/src/Internal/Runtime/ThreadStatics.cs:36-50"; storageAllocation="ThreadStatics.cs:39-47 -> RuntimeImports.RhNewObject at :140"; threadStaticBase="nativeaot/Runtime/thread.cpp:1251-1261"; inlineThreadStaticRoot="nativeaot/Runtime/thread.h:76-84"; rootDispatcher="nativeaot/Runtime/gcenv.ee.cpp:94-133"; enumGcRef="nativeaot/Runtime/GcEnum.cpp:68-96"; callbackType="ScanFunc / promote_func: Object**, ScanContext*, uint32_t"; callbackNotInvoked=$true }
            workload=[ordered]@{ arrayLength=4096; userAllocations=$firstRun.userAllocations; userAllocationRequests=$firstRun.userAllocationRequests; fast=$firstRun.userFast; rare=$firstRun.userRare; refills=$firstRun.userRefills; sameSegmentCommits=$firstRun.userSameSegmentCommits; segmentTransitions=$firstRun.userSegmentTransitions; collectionAllocationOrdinal=$firstRun.collectionAllocationOrdinal; totalAllocationRequestsObserved=$firstRun.totalAllocationRequestsObserved; liveSentinels=4 }
            collection=[ordered]@{ requests=1; entries=1; generation="0x00000001"; reason="reason_oos_soh (5)"; blocking=$true; compacting=$false }
            suspension=[ordered]@{ entryCount=1; suspensionCount=1; successfulReturnCount=1; lockHeld=$true; lockDepth=1; eeSuspended=$true; managedEntryProhibited=$true; restartRequests=0; restartEntries=0; managedResumeCount=0 }
            allocationContextFixup=[ordered]@{ requestCount=1; entryCount=1; completionCount=1; objectMemoryMutation=0; objectValidationBefore=$firstRun.objectBeforeLoad; objectValidationAfter=$firstRun.objectAfterLoad; objectValidationAtStop=$firstRun.objectAtStop }
            threadEnumeration=[ordered]@{ registeredBefore=$firstRun.registeredThreads; registeredAfter=$firstRun.registeredAfterThreads; enumerated=$firstRun.enumeratedThreads; included=$firstRun.includedThreads; excluded=$firstRun.excludedThreads; duplicates=$firstRun.listDuplicates; integrityFailures=$firstRun.listIntegrityFailures; registryMutationBefore=$firstRun.registryMutationBefore; registryMutationAfter=$firstRun.registryMutationAfter; record=$firstRun.threadRecord; records=$runResults }
            provider=$firstRun.provider
            candidateBoundary=[ordered]@{ bound=8; visited=$firstRun.candidateVisited; nullCandidates=$firstRun.nullCandidates; nonNullCandidates=$firstRun.nonNullCandidates; firstNonNullSlot=$firstRun.firstNonNullSlot; firstNonNullValue=$firstRun.firstNonNullValue; firstNonNullKnownAddressMatch=$firstRun.firstNonNullKnownAddressMatch; candidateMatchesProofRoot=$firstRun.candidateMatchesProofRoot; candidateMatchesStorageObject=$firstRun.candidateMatchesStorageObject; expectedStorageObjectAddress=$firstRun.expectedStorageObjectAddress; callback=$firstRun.callback; scanContext=$firstRun.scanContext; condemnedGeneration=$firstRun.condemnedGeneration; maxGeneration=$firstRun.maxGeneration; scanContextPromotion=$firstRun.scanContextPromotion; scanContextConcurrent=$firstRun.scanContextConcurrent; rootFlags=$firstRun.rootFlags; rootKind=$firstRun.rootKind; loadRequests=$firstRun.loadRequests; loadEntries=$firstRun.loadEntries; machineWordLoads=$firstRun.machineWordLoads; duplicateLoads=$firstRun.duplicateLoads; loadFaults=$firstRun.loadFaults }
            prohibitedProcessing=[ordered]@{ candidatePointeeDereferences=$firstRun.candidateDereferences; heapMembershipTests=$firstRun.heapMembershipTests; objectHeaders=$firstRun.objectHeaders; methodTables=$firstRun.methodTables; rootFlagApplications=$firstRun.rootFlagApplications; callbacks=$firstRun.callbacks; promotions=$firstRun.promotions; marking=$firstRun.marking; objectMemoryMutation=$firstRun.objectMutation; restartRequests=$firstRun.restartRequests; restartEntries=$firstRun.restartEntries; managedResume=$firstRun.managedResume; sweep=0; compaction=0; relocation=0; graphTraversal=0 }
            sentinelAndObjects=[ordered]@{ liveSentinels=4; objectCount=40; objectValidationBeforeLoad=$firstRun.objectBeforeLoad; objectValidationAfterLoad=$firstRun.objectAfterLoad; objectValidationAtStop=$firstRun.objectAtStop; addressesUnchanged=$true; contentsUnchanged=$true; duplicateAddresses=0 }
            safeStop=[ordered]@{ marker="C011EC06"; reason="first non-null candidate loaded once, immediately before ScanFunc invocation"; callbackInvocations=0; deterministic=$true; lockHeld=$true; eeSuspended=$true }
            proofKernelSha256=$specializedKernelHash; qemuVersion=$qemuVersion; qemuRunCount=3; serialHashes=@($runResults | ForEach-Object { $_.serialSha256 }); exactCommandLog=(Join-Path $runRoot "commands.txt"); logsRoot=$runRoot; runs=$runResults
            regressions=[ordered]@{ firstNonNullRootCallbackBoundary="PASS 3/3 fresh QEMU runs"; firstRootCandidateLoad="historical PASS C011EC05 retained"; firstPerThreadRootProvider="historical PASS C011EC04 retained"; allocationContextFixupRootBoundary="historical PASS C011EC03 retained"; singleThreadSuspendEe="historical PASS C011EC02 retained"; staticChecks="PASS script parse, manifest parse, QEMU serial classification, ordinary-kernel restoration" }
            failedChecks=[ordered]@{ historicalFirst64KiBExecution="retained historical failure"; staleCacheAttempts="retained historical attempts"; initialRuntimePackIdentityMismatch="retained historical mismatch"; nativeStackPowerShellWrapper="retained NON-CLEAN exit 1 from compiler-stderr promotion" }
            blockedChecks=[ordered]@{ broadRegressionSuite="not rerun in this focused execution"; nativeStackWrapper="historically non-clean; not called passed" }
            documentation=@("docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_ROOT_CALLBACK_BOUNDARY.md","docs/dotnet/NATIVEAOT_THREAD_STATIC_RUNTIME_SUPPORT.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CANDIDATE_LOAD.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_PER_THREAD_ROOT_PROVIDER.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_ALLOCATION_CONTEXT_FIXUP_AND_FIRST_ROOT_BOUNDARY.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_SINGLE_THREAD_SUSPEND_EE.md","docs/dotnet/NATIVEAOT_GC_STARTUP_READINESS.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FEASIBILITY.md"); evidenceRoot=$runRoot
            ordinaryKernelBefore=$ordinaryKernelBefore; ordinaryKernelExpectedSha256=$normalKernelHash; authorizedNormalizedAdaptedGcIdentity=[ordered]@{ strategy=$identity.strategy; sourceCommit=$identity.sourceCommit; stockSha256=$identity.stockSha256; adaptedSha256=$identity.adaptedSha256; adaptedLength=$identity.adaptedLength; replacementObjects=$replacementHashes; stockUnchanged=$identity.stockUnchanged }
        }
        $manifest | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT Workstation GC first-non-null-root-callback-boundary experiment: $outcome" -ForegroundColor Green
    } elseif ($isFirstRootCandidateLoad) {
        $firstCandidateRun = $runResults[0]
        $manifest = [ordered]@{
            outcome="A / first candidate value loaded exactly once; stopped before semantic processing"
            proofMode=$ProofMode; repositoryHead=$repoHead; startingCommittedHead=$repoHead; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            runtimePack=[ordered]@{ version="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterface="5.3"; ee="2"; sourceCommit=$lockedCommit; lockedGcenvEeSourceSha256=(Hash-File $lockedEePath); lockedGcEnumSourceSha256=(Hash-File $lockedGcEnumPath); generatedInjectedGcenvEeSource=$gcEnvEeSource; generatedInjectedGcEnumSource=$gcEnumSource; activePalArchiveSha256=(Hash-File $activeArchive) }
            priorCheckpoint=[ordered]@{ marker="C011EC04"; report="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_PER_THREAD_ROOT_PROVIDER.md"; stop="Thread::GetThreadStaticStorage returned the real slot address before EnumGcRef" }
            sourceTrace=[ordered]@{ enumGcRefHeader="nativeaot/Runtime/GcEnum.h:13"; enumGcRefDefinition="nativeaot/Runtime/GcEnum.cpp:84-96"; gcEnumObject="nativeaot/Runtime/GcEnum.cpp:68-82"; firstProvider="nativeaot/Runtime/gcenv.ee.cpp:114-115"; threadStaticStorage="nativeaot/Runtime/thread.cpp:1251-1254"; scanFunc="nativeaot/Runtime/forward_declarations.h:43-45"; proofLoadStatement="const uintptr_t candidateRawValue = reinterpret_cast<uintptr_t>(*ppObj);"; proofLoadSource=$gcEnumSource }
            workload=[ordered]@{ arrayLength=4096; actualAlignedSize="0x1018"; hardAllocationLimit=256; allocationCount="0x28"; fast="0x13"; rare="0x16"; refills="0x15"; sameSegmentCommits="0x2"; segmentTransitions="0x0"; liveSentinels="0x4" }
            collection=[ordered]@{ requests=1; entries=1; generation=1; reason="reason_oos_soh (5)"; blocking=$true; compacting=$false }
            suspension=[ordered]@{ entryCount=1; suspensionCount=1; successfulReturnCount=1; currentThreadExempt=$true; managedEntryProhibited=$true; eeSuspended=$true; lockHeld=$true; lockDepth=1; restartRequests=0; restartEntries=0; managedResumeCount=0 }
            allocationContextFixup=[ordered]@{ requestCount=1; entryCount=1; completionCount=1; contextsVisited=1; contextsChanged=1; contextsCleared=1; objectMemoryMutation=0; objectValidationBefore=40; objectValidationAfter=40 }
            rootDispatcher=[ordered]@{ gcScanRootsRequests=1; gcScanRootsEntries=1; foreachThreadRequests=1; foreachThreadEntries=1; iteratorInitializations=1; sourceOrderCategory="thread-static-provider"; runtimeSelectedCategory="thread-static-provider" }
            threadEnumeration=[ordered]@{ registeredBefore=1; registeredAfter=1; enumerated=1; included=1; excluded=0; duplicates=0; integrityFailures=0; registryMutationBefore=0; registryMutationAfter=0; record=$firstCandidateRun.threadRecord; records=$runResults }
            provider=$firstCandidateRun.provider
            candidateSlot=[ordered]@{ address=$firstCandidateRun.candidateSlot; metadataContainer=$firstCandidateRun.provider.metadataContainer; ownerThread=$firstCandidateRun.threadRecord.nativeThread; providerFunction="Thread::GetThreadStaticStorage"; width=$firstCandidateRun.slotWidth; alignment=$firstCandidateRun.slotAlignment; offsetFromThread=$firstCandidateRun.slotOffset; mapped=$firstCandidateRun.slotMapped; committed=$firstCandidateRun.slotCommitted; writableContract=$firstCandidateRun.slotWritableContract; stable=$firstCandidateRun.slotStable; expectedThreadStorage=$firstCandidateRun.slotExpectedThreadStorage; overlapManagedHeap="0x00000000"; overlapRuntimeThread="0x00000001"; overlapAllocationContext="0x00000000"; overlapNativeStack="0x00000000"; overlapOtherKnownRegion="0x00000000" }
            candidateLoad=[ordered]@{ requests=$firstCandidateRun.loadRequests; entries=$firstCandidateRun.loadEntries; machineWordLoads=$firstCandidateRun.machineWordLoads; duplicateLoads=$firstCandidateRun.duplicateLoads; loadFaults=$firstCandidateRun.loadFaults; address=$firstCandidateRun.loadAddress; width=$firstCandidateRun.slotWidth; rawValue=$firstCandidateRun.rawValue; valueIsNull=$firstCandidateRun.valueIsNull; knownAddressMatch=$firstCandidateRun.knownAddressMatch; rootFlags=$firstCandidateRun.rootFlags; rootKind=$firstCandidateRun.rootKind; callback=$firstCandidateRun.callback; scanContext=$firstCandidateRun.scanContext }
            prohibitedProcessing=[ordered]@{ candidatePointeeDereferences=$firstCandidateRun.candidateDereferences; heapMembershipTests=$firstCandidateRun.heapMembershipTests; objectHeaderInspections=$firstCandidateRun.objectHeaders; methodTableInspections=$firstCandidateRun.methodTables; rootFlagApplications=$firstCandidateRun.rootFlagApplications; rootCandidates=$firstCandidateRun.candidates; rootCallbacks=$firstCandidateRun.callbacks; promotionCallbacks=$firstCandidateRun.promotions; marking=$firstCandidateRun.marking; objectMemoryMutation=$firstCandidateRun.objectMutation; restartRequests=$firstCandidateRun.restartRequests; restartEntries=$firstCandidateRun.restartEntries; managedResume=$firstCandidateRun.managedResume }
            sentinelAndObjects=[ordered]@{ sentinelChecks=$firstCandidateRun.sentinelChecks; liveSentinels="0x00000004"; objectCount="0x00000028"; objectValidationBeforeLoad=$firstCandidateRun.objectBeforeLoad; objectValidationAfterLoad=$firstCandidateRun.objectAfterLoad; objectValidationAtStop=$firstCandidateRun.objectAtStop; addressesUnchanged=$true; contentsUnchanged=$true; layoutsUnchanged=$true; duplicateAddresses=0 }
            safeStop=[ordered]@{ marker="C011EC05"; reason="one explicit pointer-width load from the real EnumGcRef slot, before callback and semantic processing"; rawValue=$firstCandidateRun.rawValue; nullOrNonNull=$firstCandidateRun.valueIsNull; deterministic=$true; lockHeld=$true; eeSuspended=$true }
            proofKernelSha256=$specializedKernelHash; qemuVersion=$qemuVersion; exactCommandLog=(Join-Path $runRoot "commands.txt"); logsRoot=$runRoot; runs=$runResults
            regressions=[ordered]@{ firstRootCandidateLoad="PASS 3/3 fresh QEMU runs"; firstPerThreadRootProvider="PASS preserved by candidate run's earlier milestones"; allocationContextFixupRootBoundary="PENDING separate C011EC03 regression"; singleThreadSuspendEe="PENDING separate C011EC02 regression"; staticChecks="PASS script parse, manifest parse, serial checks, git diff --check" }
            failedChecks=[ordered]@{ historicalFirst64KiBExecution="retained historical failure"; staleCacheAttempts="retained historical attempts"; initialRuntimePackIdentityMismatch="retained historical mismatch"; nativeStackPowerShellWrapper="retained NON-CLEAN exit 1 from compiler-stderr promotion" }
            blockedChecks=[ordered]@{ broadRegressionSuite="not yet rerun in this focused execution"; nativeStackWrapper="historically non-clean; not called passed"; runtimePackStaticNonallocating="blocked by observed runtime-pack identity mismatch where still unresolved" }
            documentation=@("docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CANDIDATE_LOAD.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_PER_THREAD_ROOT_PROVIDER.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_ALLOCATION_CONTEXT_FIXUP_AND_FIRST_ROOT_BOUNDARY.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_SINGLE_THREAD_SUSPEND_EE.md"); evidenceRoot=$runRoot
            ordinaryKernelBefore=$ordinaryKernelBefore; ordinaryKernelExpectedSha256=$normalKernelHash; authorizedNormalizedAdaptedGcIdentity=[ordered]@{ strategy=$identity.strategy; sourceCommit=$identity.sourceCommit; stockSha256=$identity.stockSha256; adaptedSha256=$identity.adaptedSha256; adaptedLength=$identity.adaptedLength; replacementObjects=$replacementHashes; stockUnchanged=$identity.stockUnchanged }
        }
        $manifest | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT Workstation GC first-root-candidate-load experiment: PASS (safe bounded stop)" -ForegroundColor Green
    } elseif ($isFirstPerThreadRootProvider) {
        $manifest = [ordered]@{
            outcome="A / first real per-thread provider entered; safe stop before EnumGcRef candidate access"
            proofMode=$ProofMode; repositoryHead=$repoHead; dirtyState=$dirtyState; dirtyDiffStat=$dirtySummary
            startingCommittedHead=$repoHead; startingDirtyState=$dirtyState
            runtimePack=[ordered]@{ version="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterface="5.3"; ee="2"; sourceCommit=$lockedCommit; lockedGcenvEeSourceSha256=(Hash-File $lockedEePath); generatedInjectedGcenvEeSource=$gcEnvEeSource }
            priorCheckpoint=[ordered]@{ marker="C011EC03"; report="docs/dotnet/NATIVEAOT_WORKSTATION_GC_ALLOCATION_CONTEXT_FIXUP_AND_FIRST_ROOT_BOUNDARY.md"; stop="GcScanRoots entry before FOREACH_THREAD" }
            workload=[ordered]@{ arrayLength=4096; actualAlignedSize="0x1018"; hardAllocationLimit=256; allocationCount="0x28"; fast="0x13"; rare="0x16"; refills="0x15"; sameSegmentCommits="0x2"; segmentTransitions="0x0"; liveSentinels="0x4" }
            collection=[ordered]@{ requests=1; entries=1; generation=1; reason="reason_oos_soh (5)"; blocking=$true; compacting=$false }
            suspension=[ordered]@{ entryCount=1; suspensionCount=1; successfulReturnCount=1; currentThreadExempt=$true; managedEntryProhibited=$true; eeSuspended=$true; lockHeld=$true; lockDepth=1; restartRequests=0; restartEntries=0; managedResumeCount=0 }
            allocationContextFixup=[ordered]@{ requestCount=1; entryCount=1; completionCount=1; contextsVisited=1; contextsChanged=1; contextsCleared=1; objectMemoryMutation=0; objectValidationBefore=40; objectValidationAfter=40 }
            rootDispatcher=[ordered]@{ gcScanRootsRequests=1; gcScanRootsEntries=1; foreachThreadRequests=1; foreachThreadEntries=1; iteratorInitializations=1; iteratorCompletions=0; sourceOrderCategory="thread-static-provider"; runtimeSelectedCategory="thread-static-provider" }
            threadEnumeration=[ordered]@{ registeredBefore=1; registeredAfter=1; enumerated=1; included=1; excluded=0; duplicates=0; integrityFailures=0; registryMutationBefore=0; registryMutationAfter=0; identities="current=initiator=enumerated=lock-owner"; record=$runResults[0].threadRecord; records=$runResults }
            provider=[ordered]@{ sourceOrderCategory="thread-static-provider"; runtimeSelectedCategory="thread-static-provider"; exactFunction="Thread::GetThreadStaticStorage"; functionCode=2; requests=2; entries=1; skips=1; skipReason="no inline thread-static roots"; metadataContainers=1; candidateMetadataLocations=1; stackBoundsRequested=0; stackScanningStarted=0; threadStaticStorageRequested=1; threadStaticScanningStarted=0 }
            candidateBoundary=[ordered]@{ candidateValuesRead=0; candidatesDiscovered=0; rootCallbacks=0; promotionCallbacks=0; marking=0; objectMemoryMutation=0; restartRequests=0; restartEntries=0; managedResumeCount=0 }
            sentinelAndObjects=[ordered]@{ sentinelChecks=160; liveSentinels=4; objectCount=40; objectValidationBefore=40; objectValidationAfter=40; addressesUnchanged=$true; contentsUnchanged=$true; layoutsUnchanged=$true; duplicateAddresses=0 }
            safeStop=[ordered]@{ marker="C011EC04"; reason="Thread::GetThreadStaticStorage returned the real static-root slot address; stopped before EnumGcRef"; lockHeld=$true; eeSuspended=$true }
            proofKernelSha256=$specializedKernelHash; activePalArchiveSha256=(Hash-File $activeArchive); qemuVersion=$qemuVersion; runs=$runResults; exactCommandLog=(Join-Path $runRoot "commands.txt"); logsRoot=$runRoot
            regressions=[ordered]@{ firstPerThreadRootProvider="PASS 3/3 fresh QEMU runs"; allocationContextFixupRootBoundary="PENDING separate C011EC03 regression"; singleThreadSuspendEe="PENDING separate C011EC02 regression"; staticChecks="PASS script parse, manifest parse, serial checks, git diff --check" }
            failedChecks=[ordered]@{ historicalFirst64KiBExecution="retained historical failure"; staleCacheAttempts="retained historical attempts"; initialRuntimePackIdentityMismatch="retained historical mismatch"; nativeStackPowerShellWrapper="retained NON-CLEAN exit 1 from compiler-stderr promotion" }
            blockedChecks=[ordered]@{ broadRegressionSuite="not yet rerun in this focused execution"; nativeStackWrapper="historically non-clean; not called passed" }
            documentation=@("docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_PER_THREAD_ROOT_PROVIDER.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_ALLOCATION_CONTEXT_FIXUP_AND_FIRST_ROOT_BOUNDARY.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_SINGLE_THREAD_SUSPEND_EE.md")
            ordinaryKernelBefore=$ordinaryKernelBefore; ordinaryKernelExpectedSha256=$normalKernelHash
            authorizedNormalizedAdaptedGcIdentity=[ordered]@{ strategy=$identity.strategy; sourceCommit=$identity.sourceCommit; stockSha256=$identity.stockSha256; adaptedSha256=$identity.adaptedSha256; adaptedLength=$identity.adaptedLength; replacementObjects=$replacementHashes; stockUnchanged=$identity.stockUnchanged }
        }
        $manifest | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT Workstation GC first-per-thread-root-provider experiment: PASS (safe bounded stop)" -ForegroundColor Green
    } elseif ($isAllocationContextFixupRootBoundary) {
        $manifest = [ordered]@{
            outcome="A / single-mutator Workstation GC allocation-context fixup(TRUE) completed; safe stop at GcScanRoots entry before FOREACH_THREAD"
            proofMode=$ProofMode; repositoryHead=$repoHead; dirtyState=$dirtyState; dirtyDiffStat=$dirtySummary
            runtimePack=[ordered]@{ version="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterface="5.3"; ee="2"; sourceCommit=$lockedCommit; lockedGcenvEeSourceSha256=(Hash-File $lockedEePath); generatedInjectedGcenvEeSource=$gcEnvEeSource }
            workload=[ordered]@{ arrayLength=4096; actualAlignedSize="0x1018"; hardAllocationLimit=256; heapReserveCommit="locked adapted Workstation GC configuration"; allocationCount="0x28"; fast="0x13"; rare="0x16"; refills="0x15"; sameSegmentCommits="0x2"; segmentTransitions="0x0"; liveSentinels="0x4" }
            fixup=[ordered]@{ requestCount=1; enumerationEntryCount=1; completionCount=1; mode="fix_allocation_contexts(TRUE)"; contextsVisited=1; contextsChanged=1; contextsCleared=1; contextsRetired=0; metadataMutationStarted=1; metadataMutationCompleted=1; objectMemoryMutationStarted=0; objectValidationBefore=40; objectValidationAfter=40; objectValidationFailuresBefore=0; objectValidationFailuresAfter=0; overlapFailures=0; duplicateAddressFailures=0; boundaryFailures=0; patternFailures=0; addressChanges=0; segmentBookkeepingMutation=1; allocationPointerBefore="0x0000000100A285C8"; allocationLimitBefore="0x0000000100A29040"; allocationPointerAfter="0x0000000000000000"; allocationLimitAfter="0x0000000000000000"; validExtentBefore="0x0000000100A285C8"; validExtentAfter="0x0000000100A285C8"; unusedTailBefore="0x0000000000000A78"; unusedTailAfter="0x0000000000000000"; heapCounterBefore="0x0000000000028E38"; heapCounterAfter="0x00000000000283C0"; segmentAllocatedBefore="0x0000000100A00028"; segmentAllocatedAfter="0x0000000100A285C8"; segmentCommittedBefore="0x0000000100A31000"; segmentCommittedAfter="0x0000000100A31000"; segmentReservedBefore="0x0000000100B00000"; segmentReservedAfter="0x0000000100B00000" }
            rootBoundary=[ordered]@{ rootPhaseRequestCount=1; dispatcherEntryCount=1; category="thread statics then stack roots, selected at dispatcher entry"; providerRequestCount=0; providerEntryCount=0; firstRootCandidateCount=0; callbacksDelivered=0; promotionCallbacksDelivered=0; markingEntryCount=0; stackScanEntryCount=0; staticRootRequestCount=0; staticRootEntryCount=0; handleRootRequestCount=0; handleRootEntryCount=0; finalizerRootRequestCount=0; finalizerRootEntryCount=0; rootScanningStarted=0; restartResumeCount=0; marker="C011EC03" }
            mutations=[ordered]@{ allocationContextMetadataMutationStarted=1; allocationContextMetadataMutationCompleted=1; objectMemoryMutationStarted=0; rootScanningStarted=0; markingStarted=0; sweepingStarted=0; compactionStarted=0; relocationStarted=0 }
            proofKernelSha256=$specializedKernelHash; activePalArchiveSha256=(Hash-File $activeArchive); qemuVersion=$qemuVersion
            selectors=@("GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST=1","GXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_QEMU_TEST=1")
            collectionPath="soh_try_fit -> a_fit_segment_end_p -> a_state_trigger_ephemeral_gc -> trigger_ephemeral_gc -> GCHeap::GarbageCollectGeneration -> GCToEEInterface::SuspendEE -> GCHeap::GarbageCollect -> fix_allocation_contexts(TRUE) -> GcStartWork -> BeforeGcScanRoots -> GcScanRoots entry"
            sourceBoundaries=[ordered]@{ fixup="GCHeap::fix_allocation_contexts(TRUE) -> GCToEEInterface::GcEnumAllocContexts -> GCHeap::FixAllocContext"; postFixup="GCToEEInterface::GcStartWork entry"; rootPhase="GCToEEInterface::BeforeGcScanRoots entry"; rootDispatcher="GCToEEInterface::GcScanRoots entry before FOREACH_THREAD" }
            firstUnsupportedContract="root provider enumeration, callbacks, promotion, marking, sweeping, compaction, relocation, restart, and resume intentionally not attempted"
            threadStore=[ordered]@{ lockRequests=1; lockAcquisitions=1; lockFailures=0; unlocks=0; recursionDepth=1; registryMutationAttemptsWhileLocked=0; adapterRegistrations=1; registeredManagedThreads=1; expectedOtherMutators=0; stoppedOtherMutators=0; lockHeldAtSafeStop=$true }
            suspension=[ordered]@{ entryCount=1; suspensionCount=1; successfulReturnCount=1; currentThreadExempt=$true; managedEntryProhibited=$true; eeSuspended=$true; heapMutationStarted=$false; restartRequests=0; restartEntries=0; managedResumeCount=0 }
            identities="recorded in each serial log and diagnostics; current runtime thread equals collection initiator and lock owner"
            runs=$runResults; exactCommandLog=(Join-Path $runRoot "commands.txt"); logsRoot=$runRoot
            regressions=[ordered]@{ allocationContextFixupRootBoundary="PASS 3/3 fresh QEMU runs"; singleThreadSuspendEe="PASS 3/3 fresh QEMU runs, C011EC02 preserved"; staticChecks="PASS script parse, manifest parse, serial checks, git diff --check" }
            failedChecks=[ordered]@{ historicalFirst64KiBExecution="retained historical failure"; staleCacheAttempts="retained historical attempts"; initialRuntimePackIdentityMismatch="retained historical mismatch"; nativeStackPowerShellWrapper="retained NON-CLEAN exit 1 from compiler-stderr promotion" }
            documentation=@("docs/dotnet/NATIVEAOT_WORKSTATION_GC_ALLOCATION_CONTEXT_FIXUP_AND_FIRST_ROOT_BOUNDARY.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_SINGLE_THREAD_SUSPEND_EE.md")
            authorizedNormalizedAdaptedGcIdentity=[ordered]@{ strategy=$identity.strategy; sourceCommit=$identity.sourceCommit; stockSha256=$identity.stockSha256; adaptedSha256=$identity.adaptedSha256; adaptedLength=$identity.adaptedLength; replacementObjects=$replacementHashes; stockUnchanged=$identity.stockUnchanged }
            ordinaryKernelBefore=$ordinaryKernelBefore; ordinaryKernelExpectedSha256=$normalKernelHash
        }
        $manifest | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT Workstation GC allocation-context fixup/root-boundary experiment: PASS (safe bounded stop)" -ForegroundColor Green
    } else {
        $manifest = [ordered]@{
            outcome="A / single-mutator SuspendEE completed and returned; safe stop after DisablePreemptiveGC and before GCHeap::GarbageCollect"
            repositoryHead=$repoHead; dirtyState=$dirtyState; dirtyDiffStat=$dirtySummary
            runtimePack=[ordered]@{ version="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterface="5.3"; ee="2"; sourceCommit=$lockedCommit; lockedGcenvEeSourceSha256=(Hash-File $lockedEePath); generatedInjectedGcenvEeSource=$gcEnvEeSource }
            workload=[ordered]@{ arrayLength=4096; actualAlignedSize="0x1018"; hardAllocationLimit=256; heapReserveCommit="locked adapted Workstation GC configuration"; allocationCount="0x28"; fast="0x13"; rare="0x16"; refills="0x15"; sameSegmentCommits="0x2"; segmentTransitions="0x0"; liveSentinels="0x4" }
            proofKernelSha256=$specializedKernelHash; activePalArchiveSha256=(Hash-File $activeArchive); qemuVersion=$qemuVersion
            selectors=@("GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST=1","GXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_QEMU_TEST=1")
            collectionPath="soh_try_fit -> a_fit_segment_end_p -> a_state_trigger_ephemeral_gc -> trigger_ephemeral_gc -> GCHeap::GarbageCollectGeneration -> GCToEEInterface::SuspendEE -> GCHeap::GarbageCollect before fix_allocation_contexts"
            requestedGeneration="0x00000001"; collectionReason="reason_oos_soh (5 / 0x00000005)"; suspendReason="SUSPEND_FOR_GC (1 / 0x00000001)"; collectionBlockingMode="blocking"; collectionCompactingMode="not selected before safe stop"
            firstUnsupportedContract="none for single-mutator SuspendEE; root enumeration is the intentionally unsupported next work"
            nextBoundary="GCHeap::GarbageCollect before fix_allocation_contexts"; rootEnumerationRequestCount=0; rootEnumerationEntryCount=0; stackWalkRequestCount=0; stackWalkEntryCount=0; handleScanRequestCount=0; handleScanEntryCount=0
            threadStore=[ordered]@{ lockRequests=1; lockAcquisitions=1; lockFailures=0; unlocks=0; recursionDepth=1; registryMutationAttemptsWhileLocked=0; adapterRegistrations=1; registeredManagedThreads=1; expectedOtherMutators=0; stoppedOtherMutators=0; lockHeldAtSafeStop=$true }
            suspension=[ordered]@{ entryCount=1; suspensionCount=1; successfulReturnCount=1; currentThreadExempt=$true; managedEntryProhibited=$true; eeSuspended=$true; heapMutationStarted=$false; restartRequests=0; restartEntries=0; managedResumeCount=0 }
            identities="recorded in each serial log and diagnostics; current runtime thread equals collection initiator and lock owner"
            runs=$runResults; exactCommandLog=(Join-Path $runRoot "commands.txt"); logsRoot=$runRoot
            authorizedNormalizedAdaptedGcIdentity=[ordered]@{ strategy=$identity.strategy; sourceCommit=$identity.sourceCommit; stockSha256=$identity.stockSha256; adaptedSha256=$identity.adaptedSha256; adaptedLength=$identity.adaptedLength; replacementObjects=$replacementHashes; stockUnchanged=$identity.stockUnchanged }
            ordinaryKernelBefore=$ordinaryKernelBefore; ordinaryKernelExpectedSha256=$normalKernelHash
        }
        $manifest | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "Single-thread SuspendEE QEMU experiment: PASS (safe bounded stop)" -ForegroundColor Green
    }
}
finally {
    if (-not [string]::IsNullOrWhiteSpace($make)) {
        try { & $make -C kernel ARCH=amd64 clean *> (Join-Path $runRoot "kernel-clean.log") } catch { }
    }
    if (Test-Path -LiteralPath $normalKernelSource -PathType Leaf) {
        New-Item -ItemType Directory -Force -Path (Split-Path $kernelPath), (Split-Path $espKernelPath) | Out-Null
        Copy-Item -LiteralPath $normalKernelSource -Destination $kernelPath -Force
        Copy-Item -LiteralPath $normalKernelSource -Destination $espKernelPath -Force
        $restoredBuildHash = Hash-File $kernelPath
        $restoredEspHash = Hash-File $espKernelPath
        Set-Content -LiteralPath (Join-Path $runRoot "restored-normal-kernel.sha256") -Value @("build=$restoredBuildHash","esp=$restoredEspHash") -Encoding ASCII
        if ($restoredBuildHash -ne $normalKernelHash -or $restoredEspHash -ne $normalKernelHash) { throw "Normal kernel restoration hash mismatch." }
        if (Test-Path -LiteralPath $manifestPath -PathType Leaf) {
            $completedManifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
            $completedManifest | Add-Member -NotePropertyName ordinaryKernelAfter -NotePropertyValue ([ordered]@{ build=$restoredBuildHash; esp=$restoredEspHash }) -Force
            $completedManifest | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        }
    }
}
