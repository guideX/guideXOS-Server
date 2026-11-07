param(
    [Parameter(Mandatory=$true)][string]$CandidateRoot,
    [ValidateRange(60,300)][int]$BootTimeoutSeconds = 180,
    [ValidateRange(1,60)][int]$AdmissionWindowSeconds = 30,
    [ValidateSet('R1','R2','R3')][string]$ThreadStoreAttempt = 'R1',
    [string]$CandidateIntegrityInventory = '',
    [ValidateSet('R2R2R1','R2R2R2','R2R2R3','R2R2R4')][string]$NativeAllocationGuestAttempt = 'R2R2R1',
    [switch]$HostedPreflightOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') { throw 'C188R12R4R5 QAC guest requires PowerShell Core.' }

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Import-Module (Join-Path $PSScriptRoot 'c188r12r4r5r4-qac-admission.psm1') -Force
function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}
function Require-Hash([string]$Path,[string]$Expected,[string]$Name) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "$Name is missing: $Path" }
    $actual = Get-Sha256 $Path
    if ($actual -cne ([string]$Expected).ToUpperInvariant()) { throw "$Name hash changed after qualification." }
    return $actual
}
$candidate = [System.IO.Path]::GetFullPath($CandidateRoot)
$candidateManifestPath = Join-Path $candidate 'candidate-manifest.json'
if (-not (Test-Path -LiteralPath $candidateManifestPath -PathType Leaf)) {
    throw "Qualified candidate manifest is missing: $candidateManifestPath"
}
$candidateManifest = Get-Content -LiteralPath $candidateManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
$candidateManifestSha256 = Get-Sha256 $candidateManifestPath
$candidateIntegrityInventorySha256 = $null
$isInitAttribution = [string]$candidateManifest.mode -ceq 'InitAttribution'
$isInitReturnContract = [string]$candidateManifest.mode -ceq 'InitReturnContract'
$isThreadStoreReadiness = [string]$candidateManifest.mode -ceq 'ThreadStoreReadiness'
$isSemiSharedAttribution = [string]$candidateManifest.mode -ceq 'SemiSharedAttribution'
$isSafeHandoff = [string]$candidateManifest.mode -ceq 'NativeAotSafeHandoff'
$isTransitionAttribution = [string]$candidateManifest.mode -ceq 'NativeAotInitializeModulesTransitionAttribution'
$isNativeAllocationFailureAttribution = [string]$candidateManifest.mode -in @('NativeAllocationFailureAttribution','NativeAotSafeHandoff')
$isGcInitFailureAttribution = [string]$candidateManifest.mode -in @('GcInitFailureAttribution','SemiSharedAttribution','NativeAllocationFailureAttribution','NativeAotSafeHandoff')
if ($ThreadStoreAttempt -in @('R2','R3') -and -not $isThreadStoreReadiness) {
    throw "ThreadStoreAttempt $ThreadStoreAttempt is valid only for the C188 R4 ThreadStore candidate."
}
if ($NativeAllocationGuestAttempt -cne 'R2R2R1' -and -not $isNativeAllocationFailureAttribution) {
    throw 'NativeAllocationGuestAttempt is valid only for the C188 R2R2 native-allocation candidate.'
}
$isInitDiagnostic = $isInitAttribution -or $isInitReturnContract -or $isThreadStoreReadiness -or $isGcInitFailureAttribution -or $isSafeHandoff -or $isTransitionAttribution
$r4RuntimePackManifest = $null
$r4PalCommonMembership = $null
if ($isThreadStoreReadiness) {
    $r4RuntimePackManifestPath = [System.IO.Path]::GetFullPath([string]$candidateManifest.runtimePackManifest)
    if (-not (Test-Path -LiteralPath $r4RuntimePackManifestPath -PathType Leaf)) {
        throw "R4 runtime-pack manifest is missing: $r4RuntimePackManifestPath"
    }
    $r4RuntimePackManifest = Get-Content -LiteralPath $r4RuntimePackManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
    $r4PalCommonMembership = $r4RuntimePackManifest.archiveMembership.threadStorePalCommonReplacement
}
$phaseRoot = if ($isTransitionAttribution) {
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out\dotnet\c188r12r4r5r4r5r2r2r4r2r3-transition-attribution'))
} elseif ($isSafeHandoff) {
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out\dotnet\c188r12r4r5r4r5r2r2r4r2-safe-handoff'))
} elseif ($isNativeAllocationFailureAttribution -and $NativeAllocationGuestAttempt -ceq 'R2R2R4') {
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out\dotnet\c188r12r4r5r4r5r2r2r4-pal-capacity-fix'))
} elseif ($isNativeAllocationFailureAttribution) {
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out\dotnet\c188r12r4r5r4r5r2r2-native-allocation-attribution'))
} elseif ($isSemiSharedAttribution) {
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out\dotnet\c188r12r4r5r4r5r2-semishared-attribution'))
} elseif ($isGcInitFailureAttribution) {
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out\dotnet\c188r12r4r5r4r5-gc-init-probes'))
} elseif ($isThreadStoreReadiness) {
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out\dotnet\c188r12r4r5r4-threadstore-readiness'))
} elseif ($isInitReturnContract) {
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out\dotnet\c188r12r4r5r3-init-return-contract'))
} elseif ($isInitAttribution) {
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out\dotnet\c188r12r4r5r1-runtime-init-attribution'))
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out\dotnet\c188r12r4r5-gc-ownership-proof'))
}
if (-not (Test-QacCandidatePath -CandidateRoot $candidate -PhaseRoot $phaseRoot -RequireExisting)) {
    throw "CandidateRoot must be a unique candidate below $phaseRoot."
}
if ($isTransitionAttribution) {
    $transitionProfile = Test-QacTransitionCandidateProfile -CandidateRoot $candidate -PhaseRoot $phaseRoot `
        -RepoRoot $repo -InventoryPath $CandidateIntegrityInventory
    $candidateManifest = $transitionProfile.Manifest
    $candidateIntegrityInventorySha256 = [string]$transitionProfile.InventoryResult.InventorySha256
    $CandidateIntegrityInventory = [string]$transitionProfile.InventoryPath
} elseif ($isGcInitFailureAttribution) {
    $gcRecord = $candidateManifest.gcInitializationInternalRecord
    $r5RuntimePackManifestPath = [System.IO.Path]::GetFullPath([string]$candidateManifest.runtimePackManifest)
    if (-not (Test-Path -LiteralPath $r5RuntimePackManifestPath -PathType Leaf)) {
        throw "R4R5 runtime-pack manifest is missing: $r5RuntimePackManifestPath"
    }
    $r5RuntimePackManifest = Get-Content -LiteralPath $r5RuntimePackManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
    $r5GcReplacement = $r5RuntimePackManifest.archiveMembership.c188r12r4r5r4r5GcInitReplacement
    $r5PalReplacement = $r5RuntimePackManifest.archiveMembership.threadStorePalCommonReplacement
    $r5ProbeSources = $candidateManifest.gcInitProbeSourceFiles
    $r5StageRoot = Join-Path $candidate 'staged'
    if ($isSemiSharedAttribution -or $isNativeAllocationFailureAttribution) {
        $r5StageRoot = [System.IO.Path]::GetFullPath([string]$candidateManifest.semiSharedAttributionRecord.stagedRoot)
        $candidatePrefix = $candidate.TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
        if (-not $r5StageRoot.StartsWith($candidatePrefix,[System.StringComparison]::OrdinalIgnoreCase)) {
            throw 'R2 staged qualification root escapes the candidate directory.'
        }
    }
    $r5StageManifestPath = Join-Path $r5StageRoot 'qualification-manifest.json'
    if (-not (Test-Path -LiteralPath $r5StageManifestPath -PathType Leaf)) {
        throw "R4R5 staged qualification manifest is missing: $r5StageManifestPath"
    }
    $r5StageManifest = Get-Content -LiteralPath $r5StageManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
    $r5PatchedGcWksEntry = @($r5StageManifest.files | Where-Object { [string]$_.path -ceq 'diagnostic/runtime-pack/gcwks.cpp' })
    if ($r5PatchedGcWksEntry.Count -ne 1) { throw 'R4R5 staged inventory has no unique instrumented Workstation GC source entry.' }
    $expectedGcCandidateResult = if ($isSafeHandoff) { 'SAFE_HANDOFF_HOSTED_READY' }
        elseif ($isNativeAllocationFailureAttribution) { 'NATIVE_ALLOC_PROBES_HOSTED_READY' }
        elseif ($isSemiSharedAttribution) { 'PASS_BUILD_READY_FOR_SEMISHARED_ATTRIBUTION_GUEST' }
        else { 'PASS_BUILD_READY_FOR_GC_INIT_ATTRIBUTION_GUEST' }
    $expectedRuntimePackIdentity = if ($isSafeHandoff) {
        'guidexos-nativeaot-runtime-pack-amd64-c188r12r4r5r2r2r4-safe-handoff-v1'
    } elseif ($isNativeAllocationFailureAttribution -and -not $isSafeHandoff) {
        'guidexos-nativeaot-runtime-pack-amd64-c188r12r4r5r4r5r2r2-native-allocation-v1'
    } elseif ($isSemiSharedAttribution) {
        'guidexos-nativeaot-runtime-pack-amd64-c188r12r4r5r4r5r2-semishared-v1'
    } else { 'guidexos-nativeaot-runtime-pack-amd64-c188r12r4r5r4r5-gc-init-probes-v1' }
    if ($candidateManifest.result -cne $expectedGcCandidateResult -or
        -not [bool]$candidateManifest.runtimeInitDiagnostics -or
        -not [bool]$candidateManifest.c188R12R4R5R4R5GcInitProbes -or
        [bool]$candidateManifest.qemuLaunched -or [bool]$candidateManifest.qacReserved -or
        -not [bool]$candidateManifest.productionIsolationChecks.diagnosticAndProductionOutputsSeparate -or
        -not [bool]$candidateManifest.productionIsolationChecks.diagnosticInstrumentationAbsentFromProduction -or
        [bool]$candidateManifest.productionIsolationChecks.productionRuntimePackC188ProofProfile -or
        [bool]$candidateManifest.productionIsolationChecks.productionRuntimePackR1TraceProfile -or
        [bool]$candidateManifest.productionIsolationChecks.productionRuntimePackR3ReturnProfile -or
        -not [bool]$candidateManifest.productionIsolationChecks.productionPersistentCompositeLifecycle -or
        -not [bool]$candidateManifest.productionIsolationChecks.noGcOwnershipProofProfileInProduction -or
        -not [bool]$candidateManifest.productionNativeAotPersistentCompositeLifecycle -or
        [string]$candidateManifest.runtimePackIdentity -cne $expectedRuntimePackIdentity -or
        [string]$candidateManifest.runtimePackSourceCommit -cne '9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3' -or
        [string]$r5RuntimePackManifest.identity -cne [string]$candidateManifest.runtimePackIdentity -or
        [string]$r5RuntimePackManifest.runtimeIdentity.sourceCommit -cne '9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3' -or
        [string]$r5ProbeSources.runtimeCommit -cne '9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3' -or
        [string]$r5RuntimePackManifest.c188r12r4r5r4r5GcInitSourceCommit -cne '9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3' -or
        [string]$gcRecord.marker -cne 'C188R12R4R5R4R5_GCINIT' -or
        [int]$gcRecord.recordSizeBytes -ne 64 -or [int]$gcRecord.fieldCount -ne 16 -or
        [string]$r5RuntimePackManifest.c188r12r4r5r4r5GcInitMarker -cne 'C188R12R4R5R4R5_GCINIT' -or
        [int]$r5RuntimePackManifest.c188r12r4r5r4r5GcInitRecordSizeBytes -ne 64 -or
        [int]$r5RuntimePackManifest.c188r12r4r5r4r5GcInitFieldCount -ne 16 -or
        [string]$r5GcReplacement.sourceCommit -cne '9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3' -or
        [int]$r5GcReplacement.removedCount -ne 0 -or [int]$r5GcReplacement.archiveMemberCount -ne 1 -or
        [string]$r5GcReplacement.replacementSha256 -cne [string]$candidateManifest.gcInitRecordSha256 -or
        [string]$r5GcReplacement.sourceGcWksSha256 -cne [string]$r5ProbeSources.gcwks -or
        [string]$r5GcReplacement.instrumentedGcSha256 -cne [string]$r5ProbeSources.instrumentedGc -or
        [string]$r5GcReplacement.recordHeaderSha256 -cne [string]$r5ProbeSources.recordHeader -or
        [string]$r5GcReplacement.recordImplementationSha256 -cne [string]$r5ProbeSources.recordImplementation -or
        [string]$r5PalReplacement.stockMember -cne [string]$candidateManifest.threadStoreReadinessPalCommonStockMember -or
        [int]$r5PalReplacement.removedCount -ne 0 -or [int]$r5PalReplacement.archiveMemberCount -ne 1 -or
        [string]$r5PalReplacement.replacementObject -cne [string]$candidateManifest.threadStoreReadinessPalCommonReplacementObject -or
        [string]$r5PalReplacement.replacementSha256 -cne [string]$candidateManifest.threadStoreReadinessPalCommonReplacementSha256 -or
        [string]$candidateManifest.nativeRhInitialize.signature -cne 'extern "C" bool RhInitialize(bool isDll)' -or
        [int]$candidateManifest.nativeRhInitialize.successBoolean -ne 1 -or
        [int]$candidateManifest.nativeRhInitialize.failureBoolean -ne 0 -or
        [int]$candidateManifest.nativeRhInitialize.adapterStatusSuccess -ne 0 -or
        [int]$candidateManifest.nativeRhInitialize.adapterStatusFailure -ne 1 -or
        [bool]$candidateManifest.nativeRhInitialize.moduleRegistrationIncluded -or
        [bool]$candidateManifest.nativeRhInitialize.managedMainIncluded) {
        throw 'R4R5 GC-init candidate failed its bounded-record, pinned-runtime, startup-gate, or production-isolation preflight.'
    }
    if ($isSemiSharedAttribution -and
        (-not [bool]$candidateManifest.c188R12R4R5R4R5R2SemiSharedAttribution -or
         [string]$candidateManifest.c188R12R4R5R4R5R2Marker -cne 'C188R12R4R5R4R5R2_SEMISHARED' -or
         [int]$candidateManifest.c188R12R4R5R4R5R2OperationCapacity -ne 48 -or
         [int]$candidateManifest.c188R12R4R5R4R5R2OperationFieldCount -ne 14 -or
         [int]$candidateManifest.c188R12R4R5R4R5R2SummaryFieldCount -ne 15 -or
         [bool]$candidateManifest.productionIsolationChecks.productionRuntimePackR2SemiSharedProfile)) {
        throw 'R2 candidate failed its bounded semi-shared record or diagnostic/production isolation preflight.'
    }
    if ($isNativeAllocationFailureAttribution) {
        $nativeHostedResults = Get-Content -LiteralPath ([string]$candidateManifest.nativeAllocationHostedTestResults) -Raw | ConvertFrom-Json -ErrorAction Stop
        $nativeBranchMap = Get-Content -LiteralPath ([string]$candidateManifest.nativeAllocationFailureBranchMap) -Raw | ConvertFrom-Json -ErrorAction Stop
        $nativeManagedMapPath = [System.IO.Path]::ChangeExtension(
            [System.IO.Path]::GetFullPath([string]$candidateManifest.managedPe), '.map')
        [void](Require-Hash $nativeManagedMapPath ([string]$candidateManifest.managedMapSha256) 'R2R2 NativeAOT linker map')
        $nativeManagedMapText = Get-Content -LiteralPath $nativeManagedMapPath -Raw
        $gcEnvironmentSource = Join-Path $repo 'tools\dotnet\runtime-pack\src\gcenv\guidexos_gcenv.cpp'
        $startupImportsSource = Join-Path $repo 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_startup_imports.cpp'
        $palContractSource = Join-Path $repo 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_pal_contract.cpp'
        $kernelPalSource = Join-Path $repo 'kernel\core\nativeaot_pal_qemu_test.cpp'
        [void](Require-Hash $gcEnvironmentSource ([string]$candidateManifest.artifactHashes.nativeAllocationGcEnvironmentSource) 'R2R2 GCEvent implementation source')
        [void](Require-Hash $startupImportsSource ([string]$candidateManifest.artifactHashes.nativeAllocationStartupImportsSource) 'R2R2 startup allocation imports source')
        [void](Require-Hash $palContractSource ([string]$candidateManifest.artifactHashes.nativeAllocationPalContractSource) 'R2R2 PAL contract source')
        [void](Require-Hash $kernelPalSource ([string]$candidateManifest.artifactHashes.nativeAllocationKernelPalSource) 'R2R2 kernel PAL source')
        $gcEnvironmentText = Get-Content -LiteralPath $gcEnvironmentSource -Raw
        $startupImportsText = Get-Content -LiteralPath $startupImportsSource -Raw
        $palContractText = Get-Content -LiteralPath $palContractSource -Raw
        $kernelPalText = Get-Content -LiteralPath $kernelPalSource -Raw
        $nothrowOperatorNewMap = $nativeManagedMapText -match '(?m)^\s+[0-9A-F]{4}:[0-9A-F]{8}\s+\?\?2@YAPEAX_KAEBUnothrow_t@std@@@Z\s+[0-9A-F]{16}\s+f\s+guidexos_nativeaot_startup_imports\.production\.obj\r?$'
        $startupMallocMap = $nativeManagedMapText -match '(?m)^\s+[0-9A-F]{4}:[0-9A-F]{8}\s+guidexos_nativeaot_startup_malloc\s+[0-9A-F]{16}\s+f\s+guidexos_nativeaot_startup_imports\.production\.obj\r?$'
        $palVirtualAllocMap = $nativeManagedMapText -match '(?m)^\s+[0-9A-F]{4}:[0-9A-F]{8}\s+guidexos_nativeaot_pal_virtual_alloc\s+[0-9A-F]{16}\s+f\s+guidexos_nativeaot_pal_contract\.obj\r?$'
        $hostedOperatorNewMap = [string]$nativeHostedResults.diagnosticMapOperatorNew -ceq '??2@YAPEAX_KAEBUnothrow_t@std@@@Z -> guidexos_nativeaot_startup_imports.production.obj'
        $sourceAllocationRoute =
            $gcEnvironmentText -match 'new\s*\(std::nothrow\)\s*GCEvent::Impl\s*\(\)' -and
            $startupImportsText -match '(?s)operator new\(size_t size, const std::nothrow_t&\) noexcept\s*\{.*?guidexos_nativeaot_startup_malloc\(size\)' -and
            $startupImportsText -match '(?s)guidexos_nativeaot_startup_malloc\(size_t size\)\s*\{.*?guidexos_nativeaot_pal_virtual_alloc\(' -and
            $palContractText -match '(?s)guidexos_nativeaot_pal_virtual_alloc\(.*?g_pal_hooks\.virtual_alloc' -and
            $kernelPalText -match '(?s)startupReserve\(.*?virtual_memory::reserve\('
        if (-not [bool]$candidateManifest.c188R12R4R5R4R5R2R2NativeAllocationAttribution -or
            [string]$candidateManifest.c188R12R4R5R4R5R2R2NativeAllocationMarker -cne 'C188R12R4R5R4R5R2R2_NATIVE_ALLOC' -or
            [int]$candidateManifest.c188R12R4R5R4R5R2R2NativeAllocationAttemptCapacity -ne 8 -or
            [int]$candidateManifest.nativeAllocationBackendCapacity -ne 9 -or
            [int]$candidateManifest.c188R12R4R5R4R5R2R2NativeAllocationAttemptFieldCount -ne 25 -or
            [int]$candidateManifest.c188R12R4R5R4R5R2R2NativeAllocationSummaryFieldCount -ne 9 -or
            [string]$nativeBranchMap.marker -cne 'C188R12R4R5R4R5R2R2_NATIVE_ALLOC' -or
            $nativeHostedResults.result -cne 'PASS' -or [bool]$nativeHostedResults.qemuLaunched -or [bool]$nativeHostedResults.qacReserved -or
            [string]$nativeHostedResults.evidenceKind -cne 'hosted fixed-record and shared PAL slot-table tests plus synthetic analyzer fixtures' -or
            [string]$nativeHostedResults.gcVmSlotTableTestSemantics -cne 'shared PAL slot-table implementation with synthetic reserve/commit/release backend; no guest memory claimed' -or
            [string]$nativeHostedResults.nativeAllocationQacRunnerContract -cne 'PASS' -or
            [string]$nativeHostedResults.actualOperatorNewFailureMode -notmatch 'callback refusal is synthetic' -or
            -not $nothrowOperatorNewMap -or -not $startupMallocMap -or -not $palVirtualAllocMap -or -not $hostedOperatorNewMap -or
            -not $sourceAllocationRoute -or
            -not [bool]$candidateManifest.productionIsolationChecks.diagnosticAndProductionOutputsSeparate -or
            [bool]$candidateManifest.productionIsolationChecks.productionRuntimePackR2SemiSharedProfile -or
            -not [bool]$candidateManifest.productionIsolationChecks.diagnosticInstrumentationAbsentFromProduction -or
            -not [bool]$candidateManifest.productionIsolationChecks.productionPersistentCompositeLifecycle -or
            [bool]$candidateManifest.productionIsolationChecks.productionRuntimePackSafeHandoffProfile -or
            -not [bool]$candidateManifest.productionIsolationChecks.productionMapExcludesSafeHandoff -or
            -not [bool]$candidateManifest.productionNativeAotPersistentCompositeLifecycle) {
            throw 'R2R2 candidate failed its native-allocation record, hosted synthetic-evidence, linked-probe, or production-isolation gate.'
        }
        if ($isSafeHandoff) {
            $safeAnalyzerPath = [System.IO.Path]::GetFullPath([string]$candidateManifest.safeHandoffAnalyzer)
            $safeStageMapPath = [System.IO.Path]::GetFullPath([string]$candidateManifest.safeHandoffStageMap)
            $safeHostedResultsPath = [System.IO.Path]::GetFullPath([string]$candidateManifest.safeHandoffHostedTestResults)
            [void](Require-Hash $safeAnalyzerPath ([string]$candidateManifest.artifactHashes.safeHandoffAnalyzer) 'Safe-handoff analyzer')
            [void](Require-Hash $safeStageMapPath ([string]$candidateManifest.artifactHashes.safeHandoffStageMap) 'Safe-handoff stage map')
            [void](Require-Hash ([string]$candidateManifest.safeHandoffRunner) ([string]$candidateManifest.artifactHashes.safeHandoffRunner) 'Safe-handoff QAC runner')
            [void](Require-Hash $safeHostedResultsPath ([string]$candidateManifest.artifactHashes.safeHandoffHostedTestResults) 'Safe-handoff hosted-test result')
            $safeStageMap = Get-Content -LiteralPath $safeStageMapPath -Raw | ConvertFrom-Json -ErrorAction Stop
            $safeHostedResults = Get-Content -LiteralPath $safeHostedResultsPath -Raw | ConvertFrom-Json -ErrorAction Stop
            if ([string]$candidateManifest.result -cne 'SAFE_HANDOFF_HOSTED_READY' -or
                -not [bool]$candidateManifest.c188R12R4R5R2R2R4SafeHandoff -or
                [string]$candidateManifest.c188R12R4R5R2R2R4SafeHandoffMarker -cne 'C188R12R4R5R2R2R4_SAFE_HANDOFF' -or
                [string]$candidateManifest.c188R12R4R5R2R2R4SafeHandoffTransition -cne 'disabled before RhpReversePInvoke and InitializeModules' -or
                [int]$safeHostedResults.analyzerCaseCount -lt 13 -or
                [int]$candidateManifest.nativeAllocationBackendCapacity -ne 9 -or
                [string]$candidateManifest.initializationTraceStorageModel -notmatch '64 fixed stage slots.*one packed summary line per stage.*separate first-failure record' -or
                [bool]$safeHostedResults.qemuLaunched -or [bool]$safeHostedResults.qacReserved -or
                [string]$safeStageMap.schema -cne 'C188R12R4R5R2R2R4_SAFE_HANDOFF_STAGE_MAP_V1' -or
                [bool]$safeStageMap.laterTransitionStagesEnabled) {
                throw 'Safe-handoff candidate failed the one-boot stop boundary or hosted qualification preflight.'
            }
        }
        $originalR2R1Serial = Join-Path $repo 'out\dotnet\c188r12r4r5r4r5r2-semishared-attribution\qac-guest-r4r5r2r1-001\serial.log'
        [void](Require-Hash $originalR2R1Serial '39402A5495C0DF35492C403B776EC9214A26D64D31B2D1CA558F196124905C5B' 'Archived R2R1 raw guest serial')
        [void](Require-Hash ([string]$candidateManifest.nativeAllocationAnalyzer) `
            ([string]$candidateManifest.artifactHashes.nativeAllocationAnalyzer) 'R2R2 native-allocation analyzer')
        [void](Require-Hash ([string]$candidateManifest.nativeAllocationFailureBranchMap) `
            ([string]$candidateManifest.artifactHashes.nativeAllocationBranchMap) 'R2R2 failure-branch map')
        [void](Require-Hash ([string]$candidateManifest.nativeAllocationHostedTestResults) `
            ([string]$candidateManifest.artifactHashes.nativeAllocationHostedTests) 'R2R2 hosted test result')
        [void](Require-Hash $nativeManagedMapPath ([string]$candidateManifest.managedMapSha256) 'R2R2 NativeAOT linker map')
        if ([string]$candidateManifest.sourceRamdiskSha256 -cne 'AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8' -or
            [string]$candidateManifest.specializedKernelSha256 -cne [string]$candidateManifest.espKernelSha256 -or
            [string]$candidateManifest.managedElfSha256 -cne [string]$candidateManifest.artifactHashes.candidateNativeAotElf -or
            [string]$candidateManifest.specializedKernelSha256 -cne [string]$candidateManifest.artifactHashes.diagnosticKernelElf -or
            [string]$candidateManifest.productionNativeAotElfSha256 -cne [string]$candidateManifest.artifactHashes.productionNativeAotElf -or
            [string]$candidateManifest.productionKernelSha256 -cne [string]$candidateManifest.artifactHashes.productionKernelElf) {
            throw 'R2R2 diagnostic, source-ramdisk, or production artifact identities differ from the integrity inventory.'
        }
        if (-not $sourceAllocationRoute -or -not $nothrowOperatorNewMap -or
            -not $startupMallocMap -or -not $palVirtualAllocMap) {
            throw 'R2R2 source and hash-pinned linker map do not establish GCEvent::Impl through nothrow new to the startup PAL virtual-allocation import.'
        }
        foreach ($requiredBranch in @('0x0200','0x0201','0x0202','0x0203','0x0204','0x0205','0x0206','0x0310','0x0408','0x0413')) {
            if (($nativeBranchMap | ConvertTo-Json -Depth 8) -notmatch [regex]::Escape($requiredBranch)) {
                throw "R2R2 operation or VM failure branch is absent from the diagnostic branch map: $requiredBranch"
            }
        }
    }
} elseif ($isInitAttribution) {
    if ($candidateManifest.result -cne 'PASS_BUILD_READY_FOR_INIT_DIAGNOSTIC_QAC_PREFLIGHT' -or
        -not [bool]$candidateManifest.runtimeInitDiagnostics -or
        [bool]$candidateManifest.qemuLaunched -or [bool]$candidateManifest.qacReserved -or
        -not [bool]$candidateManifest.productionIsolationChecks.diagnosticAndProductionOutputsSeparate -or
        -not [bool]$candidateManifest.productionIsolationChecks.diagnosticInstrumentationAbsentFromProduction -or
        -not [bool]$candidateManifest.productionNativeAotPersistentCompositeLifecycle -or
        $candidateManifest.runtimePackSourceCommit -cne '9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3') {
        throw 'R1 candidate manifest failed its one-guest initialization-diagnostics or production-isolation gate.'
    }
} elseif ($isInitReturnContract) {
    if ($candidateManifest.result -cne 'PASS_BUILD_READY_FOR_INIT_RETURN_CONTRACT_GUEST' -or
        -not [bool]$candidateManifest.runtimeInitDiagnostics -or
        -not [bool]$candidateManifest.c188R12R4R5R3InitReturnContract -or
        [bool]$candidateManifest.qemuLaunched -or [bool]$candidateManifest.qacReserved -or
        -not [bool]$candidateManifest.productionIsolationChecks.diagnosticAndProductionOutputsSeparate -or
        -not [bool]$candidateManifest.productionIsolationChecks.diagnosticInstrumentationAbsentFromProduction -or
        [bool]$candidateManifest.productionIsolationChecks.productionRuntimePackC188ProofProfile -or
        [bool]$candidateManifest.productionIsolationChecks.productionRuntimePackR1TraceProfile -or
        [bool]$candidateManifest.productionIsolationChecks.productionRuntimePackR3ReturnProfile -or
        -not [bool]$candidateManifest.productionNativeAotPersistentCompositeLifecycle -or
        [string]$candidateManifest.nativeRhInitialize.signature -cne 'extern "C" bool RhInitialize(bool isDll)' -or
        [int]$candidateManifest.nativeRhInitialize.successBoolean -ne 1 -or
        [int]$candidateManifest.nativeRhInitialize.failureBoolean -ne 0 -or
        [int]$candidateManifest.nativeRhInitialize.adapterStatusSuccess -ne 0 -or
        [int]$candidateManifest.nativeRhInitialize.adapterStatusFailure -ne 1 -or
        [bool]$candidateManifest.nativeRhInitialize.moduleRegistrationIncluded -or
        [bool]$candidateManifest.nativeRhInitialize.managedMainIncluded -or
        [string]$candidateManifest.runtimePackIdentity -cne 'guidexos-nativeaot-runtime-pack-amd64-c188r12r4r5r3-init-return-contract-v1' -or
        $candidateManifest.runtimePackSourceCommit -cne '9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3') {
        throw 'R3 candidate manifest failed its one-guest return-contract, startup-gate, or production-isolation preflight.'
    }
} elseif ($isThreadStoreReadiness) {
    if ($candidateManifest.result -cne 'PASS_BUILD_READY_FOR_THREADSTORE_READINESS_GUEST' -or
        -not [bool]$candidateManifest.runtimeInitDiagnostics -or
        -not [bool]$candidateManifest.c188R12R4R5R3InitReturnContract -or
        -not [bool]$candidateManifest.c188R12R4R5R4ThreadStoreReadiness -or
        [bool]$candidateManifest.qemuLaunched -or [bool]$candidateManifest.qacReserved -or
        -not [bool]$candidateManifest.productionIsolationChecks.diagnosticAndProductionOutputsSeparate -or
        -not [bool]$candidateManifest.productionIsolationChecks.diagnosticInstrumentationAbsentFromProduction -or
        -not [bool]$candidateManifest.productionNativeAotPersistentCompositeLifecycle -or
        [bool]$candidateManifest.productionIsolationChecks.productionRuntimePackC188ProofProfile -or
        [bool]$candidateManifest.productionIsolationChecks.productionRuntimePackR1TraceProfile -or
        [bool]$candidateManifest.productionIsolationChecks.productionRuntimePackR3ReturnProfile -or
        $candidateManifest.runtimePackSourceCommit -cne '9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3' -or
        [string]$candidateManifest.threadStoreReadinessPalCommonStockMember -notmatch '(?i)^nativeaot[\\/]+Runtime[\\/]+Full[\\/]+CMakeFiles[\\/]+Runtime\.WorkstationGC\.dir[\\/]+__+[\\/]+windows[\\/]+PalRedhawkCommon\.cpp\.obj$' -or
        [string]$r4RuntimePackManifest.c188r12r4r5r4PalCommonStockMember -cne [string]$candidateManifest.threadStoreReadinessPalCommonStockMember -or
        [string]$r4RuntimePackManifest.archiveMembership.result -cne 'PASS' -or
        [string]$r4PalCommonMembership.stockMember -cne [string]$candidateManifest.threadStoreReadinessPalCommonStockMember -or
        [int]$r4PalCommonMembership.removedCount -ne 0 -or
        [int]$r4PalCommonMembership.archiveMemberCount -ne 1 -or
        [string]$r4PalCommonMembership.replacementObject -cne [string]$candidateManifest.threadStoreReadinessPalCommonReplacementObject -or
        [string]$r4PalCommonMembership.replacementSha256 -cne [string]$candidateManifest.threadStoreReadinessPalCommonReplacementSha256) {
        throw 'R4 candidate manifest failed its one-boot ThreadStore, PAL stack-bounds, pinned-runtime, or production-isolation preflight.'
    }
} elseif ($candidateManifest.result -cne 'PASS_BUILD_READY_FOR_QAC_PREFLIGHT' -or
    $candidateManifest.mode -cne 'GcOwnershipProof' -or
    [bool]$candidateManifest.imageBackedArenaPresent -or
    [int64]$candidateManifest.managedHeapBytes -ne 0 -or
    $candidateManifest.runtimePackSourceCommit -cne '9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3') {
    throw 'Candidate manifest failed the one-guest isolation, arena, or pinned-runtime gate.'
}

if ($isNativeAllocationFailureAttribution -and -not $isSafeHandoff) {
    if ([string]::IsNullOrWhiteSpace($CandidateIntegrityInventory) -or
        -not (Test-Path -LiteralPath $CandidateIntegrityInventory -PathType Leaf)) {
        throw 'R2R2 guest admission requires the reviewed full candidate-integrity inventory.'
    }
    $CandidateIntegrityInventory = [System.IO.Path]::GetFullPath($CandidateIntegrityInventory)
    $integrityInventory = Get-Content -LiteralPath $CandidateIntegrityInventory -Raw | ConvertFrom-Json -ErrorAction Stop
    if ($integrityInventory.result -cne 'PASS' -or
        [string]$integrityInventory.candidateManifest -cne [System.IO.Path]::GetFullPath($candidateManifestPath) -or
        [string]$integrityInventory.candidateManifestSha256 -cne (Get-Sha256 $candidateManifestPath) -or
        [string]$integrityInventory.candidateHead -cne [string]$candidateManifest.head -or
        [string]$integrityInventory.runtimePackSourceCommit -cne [string]$candidateManifest.runtimePackSourceCommit -or
        [int]$integrityInventory.palVmTrackingCapacity -ne 256 -or
        [int]$integrityInventory.palVmTrackingStorageBytes -ne 12288 -or
        [string]$integrityInventory.capacityCorrectionSymbol -cne 'g_gcVmSlots' -or
        [string]$integrityInventory.capacityCorrectionSymbolSizeHex -cne '00003000' -or
        [string]$integrityInventory.capacityHelperSourceSha256 -cne (Get-Sha256 (Join-Path $repo 'kernel\core\nativeaot_gc_vm_slots.h')) -or
        [string]$integrityInventory.startupPalSourceSha256 -cne (Get-Sha256 (Join-Path $repo 'kernel\core\nativeaot_pal_qemu_test.cpp')) -or
        [int]$integrityInventory.verifiedArtifactHashCount -ne @($candidateManifest.artifactHashes.PSObject.Properties).Count -or
        @($integrityInventory.inventory | Where-Object status -ne 'PASS').Count -ne 0 -or
        @($integrityInventory.problems).Count -ne 0) {
        throw 'R2R2 full candidate-integrity inventory is incomplete or no longer matches the candidate.'
    }
    $candidateIntegrityInventorySha256 = Get-Sha256 $CandidateIntegrityInventory
    [void](Require-Hash ([string]$candidateManifest.managedElf) ([string]$candidateManifest.managedElfSha256) 'R2R2 diagnostic NativeAOT ELF')
    [void](Require-Hash ([string]$candidateManifest.specializedKernel) ([string]$candidateManifest.specializedKernelSha256) 'R2R2 diagnostic kernel')
    [void](Require-Hash (Join-Path $candidateManifest.esp 'kernel.elf') ([string]$candidateManifest.espKernelSha256) 'R2R2 staged diagnostic kernel')
    [void](Require-Hash (Join-Path $candidateManifest.esp 'ramdisk.img') ([string]$candidateManifest.candidateRamdiskSha256) 'R2R2 staged diagnostic ramdisk')
    [void](Require-Hash (Join-Path $repo 'ESP\ramdisk.img') ([string]$candidateManifest.sourceRamdiskSha256) 'R2R2 preserved source ramdisk')
    [void](Require-Hash $nativeManagedMapPath ([string]$candidateManifest.managedMapSha256) 'R2R2 NativeAOT linker map')
    [void](Require-Hash ([string]$candidateManifest.productionNativeAotManifest) ([string]$candidateManifest.artifactHashes.productionNativeAotQualification) 'R2R2 proof-disabled production qualification')
    [void](Require-Hash ([string]$candidateManifest.productionKernelManifest) ([string]$candidateManifest.artifactHashes.productionKernelQualification) 'R2R2 proof-disabled production kernel qualification')
} elseif ($isSafeHandoff) {
    [void](Require-Hash ([string]$candidateManifest.managedPe) ([string]$candidateManifest.managedPeSha256) 'Safe-handoff NativeAOT PE')
    [void](Require-Hash ([string]$candidateManifest.managedElf) ([string]$candidateManifest.managedElfSha256) 'Safe-handoff diagnostic NativeAOT ELF')
    [void](Require-Hash $nativeManagedMapPath ([string]$candidateManifest.managedMapSha256) 'Safe-handoff NativeAOT map')
    [void](Require-Hash ([string]$candidateManifest.specializedKernel) ([string]$candidateManifest.specializedKernelSha256) 'Safe-handoff diagnostic kernel')
    [void](Require-Hash (Join-Path $candidateManifest.esp 'kernel.elf') ([string]$candidateManifest.espKernelSha256) 'Safe-handoff staged kernel')
    [void](Require-Hash (Join-Path $candidateManifest.esp 'ramdisk.img') ([string]$candidateManifest.candidateRamdiskSha256) 'Safe-handoff staged ramdisk')
    [void](Require-Hash (Join-Path $repo 'ESP\ramdisk.img') ([string]$candidateManifest.sourceRamdiskSha256) 'Safe-handoff preserved source ramdisk')
    [void](Require-Hash ([string]$candidateManifest.productionNativeAotManifest) ([string]$candidateManifest.artifactHashes.productionNativeAotQualification) 'Safe-handoff proof-disabled production qualification')
    [void](Require-Hash ([string]$candidateManifest.productionKernelManifest) ([string]$candidateManifest.artifactHashes.productionKernelQualification) 'Safe-handoff proof-disabled production kernel qualification')
}

$candidateEsp = [System.IO.Path]::GetFullPath([string]$candidateManifest.esp)
$runCorrelationId = if ($isTransitionAttribution) {
    'C188R12R4R5R2R3R1-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + [guid]::NewGuid().ToString('N')
} else { $null }
$runDirectory = if ($isTransitionAttribution) { "qac-guest-transition-attribution-$runCorrelationId" }
    elseif ($isSafeHandoff) { 'qac-guest-r2r2r4r2r1-001' }
    elseif ($isNativeAllocationFailureAttribution -and $NativeAllocationGuestAttempt -ceq 'R2R2R4') { 'qac-guest-r2r2r4-001' }
    elseif ($isNativeAllocationFailureAttribution -and $NativeAllocationGuestAttempt -ceq 'R2R2R3') { 'qac-guest-r2r2r3-001' }
    elseif ($isNativeAllocationFailureAttribution -and $NativeAllocationGuestAttempt -ceq 'R2R2R2') { 'qac-guest-r2r2r2-001' }
    elseif ($isNativeAllocationFailureAttribution) { 'qac-guest-r2r2r1-001' }
    elseif ($isSemiSharedAttribution) { 'qac-guest-r4r5r2r1-001' }
    elseif ($isGcInitFailureAttribution) { 'qac-guest-r4r5r1-001' }
    elseif ($isThreadStoreReadiness -and $ThreadStoreAttempt -eq 'R2') { 'qac-guest-002' }
    elseif ($isThreadStoreReadiness -and $ThreadStoreAttempt -eq 'R3') { 'qac-guest-003' }
    else { 'qac-guest-001' }
$runRoot = if ($isGcInitFailureAttribution -or $isSafeHandoff -or $isTransitionAttribution) { Join-Path $phaseRoot $runDirectory } else { Join-Path $candidate $runDirectory }
$esp = $candidateEsp
$serial = Join-Path $runRoot 'serial.log'
$qmpLog = Join-Path $runRoot 'qmp.log'
$qacCli = 'D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe'
$qemu = 'C:\Program Files\qemu\qemu-system-x86_64.exe'
$qemuShare = Join-Path (Split-Path -Parent $qemu) 'share'
$ovmf = if ($isTransitionAttribution) { Join-Path $qemuShare 'edk2-x86_64-code.fd' } else { Join-Path $repo 'OVMF.fd' }
$uefiVarsTemplate = if ($isTransitionAttribution) { Join-Path $qemuShare 'edk2-i386-vars.fd' } else { $null }
$uefiVarsImage = if ($isTransitionAttribution) { Join-Path $runRoot 'uefi-vars.fd' } else { $null }
$runnerScriptSha256 = Get-Sha256 $PSCommandPath
$attemptPath = if ($isTransitionAttribution) {
    $legacyAttemptPath = Join-Path $phaseRoot 'guest-launch-attempt-C188R12R4R5R2R3R1.json'
    $candidateAttemptPath = Join-Path $phaseRoot ("guest-launch-attempt-" +
        [System.IO.Path]::GetFileName($candidate) + '.json')
    if (Test-Path -LiteralPath $legacyAttemptPath -PathType Leaf) {
        $legacyAttempt = Get-Content -LiteralPath $legacyAttemptPath -Raw | ConvertFrom-Json -ErrorAction Stop
        if ([string]::IsNullOrWhiteSpace([string]$legacyAttempt.candidateRoot)) {
            throw 'Existing transition one-boot guard has no candidate identity; refusing to select a new guard.'
        }
        $legacyCandidatePath = [System.IO.Path]::GetFullPath([string]$legacyAttempt.candidateRoot)
        if ([string]::Equals($legacyCandidatePath,$candidate,[System.StringComparison]::OrdinalIgnoreCase)) {
            $legacyAttemptPath
        } else { $candidateAttemptPath }
    } else { $candidateAttemptPath }
} elseif ($isSafeHandoff) {
    Join-Path $phaseRoot 'guest-launch-attempt-C188R12R4R5R4R5R2R2R4R2R1.json'
} elseif ($isNativeAllocationFailureAttribution) {
    Join-Path $phaseRoot "guest-launch-attempt-C188R12R4R5R4R5$NativeAllocationGuestAttempt.json"
} elseif ($isSemiSharedAttribution) {
    Join-Path $phaseRoot 'guest-launch-attempt-C188R12R4R5R4R5R2R1.json'
} elseif ($isGcInitFailureAttribution) {
    Join-Path $phaseRoot 'guest-launch-attempt-C188R12R4R5R4R5R1.json'
} elseif ($isThreadStoreReadiness -and $ThreadStoreAttempt -eq 'R2') {
    Join-Path $phaseRoot 'guest-launch-attempt-C188R12R4R5R4R2.json'
} elseif ($isThreadStoreReadiness -and $ThreadStoreAttempt -eq 'R3') {
    Join-Path $phaseRoot 'guest-launch-attempt-C188R12R4R5R4R3.json'
} else { Join-Path $phaseRoot 'guest-launch-attempt.json' }
if ($isTransitionAttribution) { $requiredGuestInputs = @($qacCli,$qemu,$ovmf,$uefiVarsTemplate) + @(
        (Join-Path $candidateEsp 'kernel.elf'),(Join-Path $candidateEsp 'ramdisk.img'),
        (Join-Path $candidateEsp 'EFI\BOOT\BOOTX64.EFI'))
} else { $requiredGuestInputs = @($qacCli,$qemu,$ovmf,(Join-Path $candidateEsp 'kernel.elf'),
        (Join-Path $candidateEsp 'ramdisk.img'),(Join-Path $candidateEsp 'EFI\BOOT\BOOTX64.EFI')) }
foreach ($required in $requiredGuestInputs) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Required guest input is missing: $required" }
}
$qemuSha256 = Get-Sha256 $qemu
if ((Get-Sha256 (Join-Path $esp 'kernel.elf')) -cne ([string]$candidateManifest.espKernelSha256).ToUpperInvariant() -or
    (Get-Sha256 (Join-Path $esp 'ramdisk.img')) -cne ([string]$candidateManifest.candidateRamdiskSha256).ToUpperInvariant() -or
    (Get-Sha256 (Join-Path $esp 'ramdisk.img')) -cne 'AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8') {
    throw 'The isolated guest media no longer matches the qualified candidate and preserved source ramdisk.'
}
if ($isThreadStoreReadiness) {
    if ([string]$candidateManifest.sourceRamdiskSha256 -cne 'AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8' -or
        [string]$candidateManifest.specializedKernelSha256 -cne [string]$candidateManifest.espKernelSha256) {
        throw 'R4 candidate source or staged-kernel identity does not match the qualified build manifest.'
    }
    [void](Require-Hash ([string]$candidateManifest.managedPe) ([string]$candidateManifest.managedPeSha256) 'R4 NativeAOT PE')
    [void](Require-Hash ([string]$candidateManifest.managedElf) ([string]$candidateManifest.managedElfSha256) 'R4 NativeAOT ELF')
    [void](Require-Hash ([string]$candidateManifest.specializedKernel) ([string]$candidateManifest.specializedKernelSha256) 'R4 isolated kernel')
    [void](Require-Hash ([string]$candidateManifest.runtimePackManifest) ([string]$candidateManifest.runtimePackManifestSha256) 'R4 runtime-pack manifest')
    [void](Require-Hash ([string]$candidateManifest.threadStoreReadinessPalCommonReplacementObject) `
        ([string]$candidateManifest.threadStoreReadinessPalCommonReplacementSha256) 'R4 stack-bounds PAL replacement')
}
if ($isGcInitFailureAttribution) {
    if (-not $isSemiSharedAttribution -and -not $isNativeAllocationFailureAttribution -and
        ([string]$candidateManifest.sourceRamdiskSha256 -cne 'AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8' -or
        [string]$candidateManifest.specializedKernelSha256 -cne [string]$candidateManifest.espKernelSha256 -or
        [string]$candidateManifest.specializedKernelSha256 -cne '714A7DBC77EDC32513221F63B6789BC0FED267A3495C5EF54F5E886ADCEB3341' -or
        [string]$candidateManifest.managedElfSha256 -cne '928A3BBC995CEC67E60885C7159FC8EE6B16BD641A3A0B5C23593ABAA3F080C5' -or
        [string]$candidateManifest.productionNativeAotElfSha256 -cne 'A69CABA99C4EF0BF7E0DA46F9C3BC91F3B30AF22E744F557C97CA84CCB695BF0' -or
        [string]$candidateManifest.productionKernelSha256 -cne 'A21A951C8DF337CEA08A7FE93580C556F0AC87F33D1B1766070FFF204B1EA168')) {
        throw 'R4R5 diagnostic or production artifact identity differs from the qualified candidate.'
    }
    [void](Require-Hash ([string]$candidateManifest.managedPe) ([string]$candidateManifest.managedPeSha256) 'R4R5 NativeAOT PE')
    [void](Require-Hash ([string]$candidateManifest.managedElf) ([string]$candidateManifest.managedElfSha256) 'R4R5 diagnostic ELF')
    [void](Require-Hash ([string]$candidateManifest.specializedKernel) ([string]$candidateManifest.specializedKernelSha256) 'R4R5 isolated kernel')
    [void](Require-Hash ([string]$candidateManifest.runtimePackManifest) ([string]$candidateManifest.runtimePackManifestSha256) 'R4R5 runtime-pack manifest')
    if (-not $isSemiSharedAttribution -and -not $isNativeAllocationFailureAttribution -and ([string]$candidateManifest.gcInitRecordSha256 -cne 'C0503979BBB13DCBB51F29A74B6C5A306D303414FFF51E2FCAD55515BDC5387A' -or
        [string]$r5RuntimePackManifest.c188r12r4r5r4r5GcInitRecordObjectSha256 -cne [string]$candidateManifest.gcInitRecordSha256)) {
        throw 'R4R5 bounded GC-init record implementation identity differs from the qualified candidate.'
    }
    [void](Require-Hash ([string]$r5RuntimePackManifest.c188r12r4r5r4r5GcInitRecordObject) `
        ([string]$candidateManifest.gcInitRecordSha256) 'R4R5 instrumented Workstation GC object')
    [void](Require-Hash ([string]$r5RuntimePackManifest.archiveMembership.archive) `
        ([string]$r5RuntimePackManifest.archiveMembership.archiveSha256) 'R4R5 Workstation GC archive')
    [void](Require-Hash ([string]$candidateManifest.threadStoreReadinessPalCommonReplacementObject) `
        ([string]$candidateManifest.threadStoreReadinessPalCommonReplacementSha256) 'R4R5 ThreadStore PAL replacement')
    $gcSourceRoot = Join-Path $candidate 'runtime-pack\gcinit-workstation-source'
    $r2StagedRuntimeSource = Join-Path $r5StageRoot 'diagnostic\runtime-pack'
    [void](Require-Hash (Join-Path $repo 'out\dotnet\gc-feasibility-baseline\nativeaot-runtime\src\coreclr\gc\gcwks.cpp') `
        ([string]$r5ProbeSources.gcwks) 'Pinned Workstation GC source input')
    [void](Require-Hash (Join-Path $gcSourceRoot 'gcwks.cpp') ([string]$r5PatchedGcWksEntry[0].sha256) 'Instrumented Workstation GC wrapper source')
    [void](Require-Hash (Join-Path $r2StagedRuntimeSource 'gcwks.cpp') `
        ([string]$r5PatchedGcWksEntry[0].sha256) 'Staged instrumented Workstation GC wrapper source')
    [void](Require-Hash (Join-Path $gcSourceRoot 'gc.cpp') ([string]$r5ProbeSources.instrumentedGc) 'Instrumented pinned GC source')
    [void](Require-Hash (Join-Path $repo 'out\dotnet\gc-feasibility-baseline\nativeaot-runtime\src\coreclr\gc\gc.cpp') `
        ([string]$r5ProbeSources.gc) 'Pinned GC companion source')
    [void](Require-Hash (Join-Path $repo 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_gcinit_record.h') `
        ([string]$r5ProbeSources.recordHeader) 'GC-init record header')
    [void](Require-Hash (Join-Path $repo 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_gcinit_record.cpp') `
        ([string]$r5ProbeSources.recordImplementation) 'GC-init record implementation source')
    if ($isSemiSharedAttribution -or $isNativeAllocationFailureAttribution) {
        $r2Sources = $candidateManifest.semiSharedSourceFiles
        $r2StageEntries = @($r5StageManifest.files | Where-Object { [string]$_.path -like 'diagnostic/runtime-pack/guidexos_nativeaot_semishared_record.*' })
        if ([int]$r2StageEntries.Count -ne 2 -or
            [string]$r5RuntimePackManifest.c188r12r4r5r4r5r2SemiSharedMarker -cne 'C188R12R4R5R4R5R2_SEMISHARED' -or
            [int]$r5RuntimePackManifest.c188r12r4r5r4r5r2OperationCapacity -ne 48 -or
            [int]$r5RuntimePackManifest.c188r12r4r5r4r5r2OperationFieldCount -ne 14 -or
            [int]$r5RuntimePackManifest.c188r12r4r5r4r5r2SummaryFieldCount -ne 15) {
            throw 'R2 record layout or staged source inventory failed the preflight.'
        }
        [void](Require-Hash (Join-Path $repo 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_semishared_record.h') `
            ([string]$r2Sources.recordHeader) 'R2 source-defined operation map')
        [void](Require-Hash (Join-Path $repo 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_semishared_record.cpp') `
            ([string]$r2Sources.recordImplementation) 'R2 fixed record implementation')
        [void](Require-Hash (Join-Path $gcSourceRoot 'gc.cpp') ([string]$r2Sources.instrumentedGc) 'R2 instrumented pinned GC source')
        [void](Require-Hash (Join-Path $r2StagedRuntimeSource 'gc.cpp') ([string]$r2Sources.instrumentedGc) 'R2 staged pinned GC source')
        [void](Require-Hash (Join-Path $r2StagedRuntimeSource 'guidexos_nativeaot_semishared_record.h') `
            ([string]$r2Sources.recordHeader) 'R2 staged operation map')
        [void](Require-Hash (Join-Path $r2StagedRuntimeSource 'guidexos_nativeaot_semishared_record.cpp') `
            ([string]$r2Sources.recordImplementation) 'R2 staged fixed record implementation')
    }
}
if (-not $HostedPreflightOnly -and (Test-Path -LiteralPath $attemptPath)) {
    throw "The candidate already has a QAC guest attempt; refusing any second launch: $attemptPath"
}
if ($isTransitionAttribution) {
    $qemuCodeSha256 = Get-Sha256 $ovmf
    $uefiVarsTemplateSha256 = Get-Sha256 $uefiVarsTemplate
    $guestState = New-QacTransitionGuestState -CandidateEsp $candidateEsp -CandidateRoot $candidate `
        -PhaseRoot $phaseRoot -RunRoot $runRoot -UefiVarsTemplate $uefiVarsTemplate
    $esp = [string]$guestState.Esp
    $uefiVarsImage = [string]$guestState.UefiVarsImage
    if ((Get-Sha256 (Join-Path $esp 'kernel.elf')) -cne ([string]$candidateManifest.espKernelSha256).ToUpperInvariant() -or
        (Get-Sha256 (Join-Path $esp 'ramdisk.img')) -cne ([string]$candidateManifest.candidateRamdiskSha256).ToUpperInvariant() -or
        (Get-Sha256 (Join-Path $esp 'EFI\BOOT\BOOTX64.EFI')) -cne ([string]$candidateManifest.artifactHashes.stagedEspBootloader).ToUpperInvariant()) {
        throw 'Fresh writable guest ESP does not match the immutable transition candidate.'
    }
} else {
    if (Test-Path -LiteralPath $runRoot) { throw "Refusing to overwrite guest evidence: $runRoot" }
    New-Item -ItemType Directory -Path $runRoot | Out-Null
    $guestState = $null
    $qemuCodeSha256 = $null
    $uefiVarsTemplateSha256 = $null
}

$manifestPath = Join-Path $runRoot 'guest-manifest.json'
$qacBeforeStatusPath = Join-Path $runRoot 'qac-before.status.txt'
$qacBeforeListPath = Join-Path $runRoot 'qac-before.list.txt'
$qacDuringStatusPath = $null
$qacDuringListPath = $null
$qacAfterStatusPath = $null
$qacAfterListPath = $null
$qacSnapshotRecords = [System.Collections.Generic.List[object]]::new()

function Get-QacSnapshot([string]$Prefix) {
    $statusLines = & $qacCli status 2>&1
    $statusExit = $LASTEXITCODE
    $listLines = & $qacCli list 2>&1
    $listExit = $LASTEXITCODE
    $statusText = ($statusLines | Out-String)
    $listText = ($listLines | Out-String)
    $statusPath = Join-Path $runRoot "$Prefix.status.txt"
    $listPath = Join-Path $runRoot "$Prefix.list.txt"
    foreach ($path in @($statusPath,$listPath)) {
        if (Test-Path -LiteralPath $path) { throw "Refusing to overwrite captured QAC snapshot: $path" }
    }
    [System.IO.File]::WriteAllText($statusPath,$statusText,[System.Text.UTF8Encoding]::new($false))
    [System.IO.File]::WriteAllText($listPath,$listText,[System.Text.UTF8Encoding]::new($false))
    $snapshot = [pscustomobject]@{
        Status=$statusText;List=$listText;StatusExit=$statusExit;ListExit=$listExit
        StatusPath=$statusPath;ListPath=$listPath;CapturedAtUtc=[DateTime]::UtcNow.ToString('o')
    }
    $qacSnapshotRecords.Add([pscustomobject]@{
        capturedAtUtc=$snapshot.CapturedAtUtc;statusExitCode=$statusExit;listExitCode=$listExit
        statusPath=$statusPath;listPath=$listPath
    })
    return $snapshot
}
function Get-QacInspection([string]$ReservationId,[string]$Prefix) {
    $inspectionPath = Join-Path $runRoot "$Prefix.inspect.json"
    if (Test-Path -LiteralPath $inspectionPath) { throw "Refusing to overwrite captured QAC reservation inspection: $inspectionPath" }
    $inspectionLines = & $qacCli inspect $ReservationId 2>&1
    $inspectionExit = $LASTEXITCODE
    $inspectionText = ($inspectionLines | Out-String)
    [System.IO.File]::WriteAllText($inspectionPath,$inspectionText,[System.Text.UTF8Encoding]::new($false))
    $inspection = $null
    if ($inspectionExit -eq 0) {
        try { $inspection = $inspectionText | ConvertFrom-Json -ErrorAction Stop } catch { $inspection = $null }
    }
    return [pscustomobject]@{
        ExitCode=$inspectionExit;Text=$inspectionText;Inspection=$inspection;Path=$inspectionPath
    }
}
function Get-WindowsQemuProcessCensus([string]$Prefix) {
    $censusPath = Join-Path $runRoot "$Prefix.windows-qemu-census.json"
    if (Test-Path -LiteralPath $censusPath) { throw "Refusing to overwrite Windows process census: $censusPath" }
    $complete = $false
    $processRows = @()
    $errorText = $null
    try {
        $allProcesses = @(Get-CimInstance -ClassName Win32_Process -ErrorAction Stop)
        $processRows = @($allProcesses | Where-Object { [string]$_.Name -match '(?i)^qemu-system-.+\.exe$' } |
            ForEach-Object {
                [ordered]@{
                    processId=[int]$_.ProcessId
                    parentProcessId=[int]$_.ParentProcessId
                    name=[string]$_.Name
                    executablePath=[string]$_.ExecutablePath
                    creationDate=[string]$_.CreationDate
                    commandLine=[string]$_.CommandLine
                }
            })
        $complete = $true
    } catch { $errorText = $_.Exception.Message }
    $census = [ordered]@{
        capturedAtUtc=[DateTime]::UtcNow.ToString('o')
        complete=$complete
        processNameFilter='^qemu-system-.+\.exe$'
        processCount=$processRows.Count
        processes=@($processRows)
        error=$errorText
    }
    [System.IO.File]::WriteAllText($censusPath,($census | ConvertTo-Json -Depth 8),[System.Text.UTF8Encoding]::new($false))
    return [pscustomobject]@{Complete=$complete;Processes=@($processRows);Path=$censusPath;Error=$errorText}
}
function Get-QacReservationInspections([object]$Snapshot,[string]$Prefix) {
    $summaryPath = Join-Path $runRoot "$Prefix.inspections.json"
    if (Test-Path -LiteralPath $summaryPath) { throw "Refusing to overwrite QAC inspection summary: $summaryPath" }
    $list = ConvertFrom-QacListSnapshot -Text ([string]$Snapshot.List) -ExitCode ([int]$Snapshot.ListExit)
    $complete = [bool]$list.Valid
    $records = [System.Collections.Generic.List[object]]::new()
    if ($list.Valid) {
        foreach ($reservation in $list.ReservationRows) {
            $capture = Get-QacInspection ([string]$reservation.Id) ("$Prefix-$($reservation.Id)")
            $inspectionId = Get-QacReservationProperty $capture.Inspection 'id'
            $valid = $capture.ExitCode -eq 0 -and $null -ne $capture.Inspection -and
                [string]$inspectionId -ceq [string]$reservation.Id
            if (-not $valid) { $complete = $false }
            $records.Add([pscustomobject]@{
                reservationId=[string]$reservation.Id
                exitCode=$capture.ExitCode
                inspectionPath=$capture.Path
                inspectionId=[string]$inspectionId
                valid=$valid
            })
        }
    }
    $summary = [ordered]@{
        capturedAtUtc=[DateTime]::UtcNow.ToString('o')
        complete=$complete
        listPath=$Snapshot.ListPath
        reservationCount=$list.ReservationCount
        inspections=@($records.ToArray())
    }
    [System.IO.File]::WriteAllText($summaryPath,($summary | ConvertTo-Json -Depth 8),[System.Text.UTF8Encoding]::new($false))
    return [pscustomobject]@{Complete=$complete;Count=$records.Count;Records=@($records.ToArray());Path=$summaryPath}
}
function Test-ModuleRegistrationStageObserved([object]$Analysis) {
    if ($null -eq $Analysis) { return $null }
    $stageProperty = $Analysis.PSObject.Properties['stages']
    if ($null -eq $stageProperty) { return $false }
    return @($stageProperty.Value | Where-Object { $_.stageName -eq 'moduleRegistration' }).Count -gt 0
}
function Get-AnalysisBoolean([object]$Analysis,[string]$PropertyName) {
    if ($null -eq $Analysis) { return $false }
    $property = $Analysis.PSObject.Properties[$PropertyName]
    if ($null -eq $property) { return $false }
    return [bool]$property.Value
}
function Read-GuestSerial {
    if (-not (Test-Path -LiteralPath $serial -PathType Leaf)) { return '' }
    $stream = [System.IO.FileStream]::new($serial,[System.IO.FileMode]::Open,[System.IO.FileAccess]::Read,
        [System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete)
    try {
        $reader = [System.IO.StreamReader]::new($stream,[System.Text.Encoding]::UTF8,$true,4096,$true)
        try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
    } finally { $stream.Dispose() }
}
function Test-NativeAllocationTraceComplete([string]$Text) {
    $marker = 'C188R12R4R5R4R5R2R2_NATIVE_ALLOC'
    $pattern = '\[' + [regex]::Escape($marker) + '\] source=(runtime|backend) invocation=([0-9A-Fa-f]{8}) sequence=([0-9A-Fa-f]{8}) field=([0-9A-Fa-f]{8}) value=([0-9A-Fa-f]{8}) auxiliary=([0-9A-Fa-f]{8})'
    $rows = @{}
    $invocations = @{}
    foreach ($match in [regex]::Matches($Text,$pattern)) {
        $sourceName = $match.Groups[1].Value
        $invocation = $match.Groups[2].Value.ToUpperInvariant()
        $sequence = $match.Groups[3].Value.ToUpperInvariant()
        $field = $match.Groups[4].Value.ToUpperInvariant()
        $key = "$sourceName/$invocation/$sequence/$field"
        if ($rows.ContainsKey($key)) { return $false }
        $rows[$key] = [pscustomobject]@{value=$match.Groups[5].Value;auxiliary=$match.Groups[6].Value}
        $invocations[$invocation] = $true
    }
    if ($invocations.Count -ne 1) { return $false }
    $invocation = [string]@($invocations.Keys)[0]
    foreach ($field in 0..8) {
        if (-not $rows.ContainsKey(('runtime/{0}/FFFFFFFF/{1:X8}' -f $invocation,(0x80000000 + $field)))) { return $false }
    }
    $runtimeCountKey = "runtime/$invocation/FFFFFFFF/80000002"
    $runtimeOverflowKey = "runtime/$invocation/FFFFFFFF/80000003"
    $runtimeCount = [Convert]::ToUInt32($rows[$runtimeCountKey].value,16)
    if ($runtimeCount -eq 0 -or $runtimeCount -gt 8 -or
        [Convert]::ToUInt32($rows[$runtimeOverflowKey].value,16) -ne 0) { return $false }
    for ($sequence = 0; $sequence -lt $runtimeCount; $sequence++) {
        for ($field = 0; $field -lt 25; $field++) {
            if (-not $rows.ContainsKey(('runtime/{0}/{1:X8}/{2:X8}' -f $invocation,$sequence,$field))) { return $false }
        }
    }
    $backendSummaryKey = "backend/$invocation/FFFFFFFF/80000000"
    $backendFirstFailureKey = "backend/$invocation/FFFFFFFF/80000001"
    if (-not $rows.ContainsKey($backendSummaryKey) -or -not $rows.ContainsKey($backendFirstFailureKey)) { return $false }
    $backendCount = [Convert]::ToUInt32($rows[$backendSummaryKey].value,16)
    $backendOverflow = [Convert]::ToUInt32($rows[$backendSummaryKey].auxiliary,16)
    if ($backendCount -gt 9 -or $backendOverflow -ne 0) { return $false }
    for ($sequence = 0; $sequence -lt $backendCount; $sequence++) {
        for ($field = 0; $field -lt 34; $field++) {
            if (-not $rows.ContainsKey(('backend/{0}/{1:X8}/{2:X8}' -f $invocation,$sequence,$field))) { return $false }
        }
    }
    return $true
}
function Get-NativeAllocationOutcome([bool]$SourceRamdiskPreserved,[bool]$PreflightPassed,[bool]$Admitted,
    [bool]$ReleaseVerified,[object]$QacExitCode,[bool]$SerialCaptured,[bool]$RawSerialPreserved,
    [bool]$RecordComplete,[object]$AnalysisError,[string]$Classification) {
    if (-not $SourceRamdiskPreserved) { return 'REGRESSION_DETECTED' }
    if (-not $PreflightPassed -or -not $Admitted) { return 'QAC_OR_EXECUTION_BLOCKED' }
    if ($SerialCaptured -and -not $RawSerialPreserved) { return 'REGRESSION_DETECTED' }
    if (-not $ReleaseVerified -or $null -eq $QacExitCode -or [int]$QacExitCode -ne 0) {
        return 'QAC_OR_EXECUTION_BLOCKED'
    }
    if (-not $RecordComplete -or $null -ne $AnalysisError -or
        $Classification -match '^INCOMPLETE_|^INCONSISTENT_') {
        return 'DIAGNOSTIC_INCONCLUSIVE'
    }
    if ($Classification -eq 'SUCCESS') { return 'NATIVE_ALLOCATION_GUEST_ACCEPTED' }
    if ($Classification -in @('PAL_NOT_READY','NATIVE_ALLOC_SIZE_OVERFLOW','KERNEL_PAL_SLOT_CAPACITY',
            'VM_ROLLBACK_RELEASE_FAILURE','BACKEND_NULL_CAUSE_UNRECORDED') -or
        $Classification -match '^VM_(RESERVE|COMMIT)_BRANCH_0x[0-9A-F]{4}$') {
        return 'NATIVE_ALLOC_FAILURE_SITE_PROVEN'
    }
    return 'DIAGNOSTIC_INCONCLUSIVE'
}
function Read-QmpResponse([System.IO.StreamReader]$Reader,[string]$Command) {
    while ($true) {
        $line = $Reader.ReadLine()
        if ($null -eq $line) { throw "QMP closed while waiting for $Command." }
        Add-Content -LiteralPath $qmpLog -Value "rx $line" -Encoding utf8
        try { $message = $line | ConvertFrom-Json -ErrorAction Stop } catch { continue }
        $qmpErrorField = $message.PSObject.Properties['error']
        if ($null -ne $qmpErrorField -and $null -ne $qmpErrorField.Value) {
            throw "QMP $Command error: $line"
        }
        $qmpReturnField = $message.PSObject.Properties['return']
        if ($null -ne $qmpReturnField -or $line -match '"return"\s*:') { return $line }
    }
}
function Invoke-Qmp([string]$Command) {
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $client.Connect('127.0.0.1',$qmpPort)
        $stream = $client.GetStream(); $stream.ReadTimeout = 5000
        $reader = [System.IO.StreamReader]::new($stream,[System.Text.Encoding]::UTF8,$false,1024,$true)
        $writer = [System.IO.StreamWriter]::new($stream,[System.Text.UTF8Encoding]::new($false),1024,$true)
        $writer.AutoFlush = $true
        $greeting = $reader.ReadLine()
        Add-Content -LiteralPath $qmpLog -Value "greeting $greeting" -Encoding utf8
        $greetingObject = $null
        try { $greetingObject = $greeting | ConvertFrom-Json -ErrorAction Stop } catch { }
        if ($null -eq $greetingObject -or $null -eq $greetingObject.PSObject.Properties['QMP']) {
            throw 'QMP greeting did not contain the required QMP object.'
        }
        $writer.WriteLine('{"execute":"qmp_capabilities"}')
        [void](Read-QmpResponse $reader 'qmp_capabilities')
        $request = '{"execute":"' + $Command + '"}'
        Add-Content -LiteralPath $qmpLog -Value "tx $request" -Encoding utf8
        $writer.WriteLine($request)
        return Read-QmpResponse $reader $Command
    } finally { $client.Dispose() }
}
function Wait-Qmp([int]$Seconds,[System.Diagnostics.Process]$Owner) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    do {
        $client = [System.Net.Sockets.TcpClient]::new()
        try { $client.Connect('127.0.0.1',$qmpPort); return $true }
        catch { if ($Owner.HasExited) { return $false }; Start-Sleep -Milliseconds 200 }
        finally { $client.Dispose() }
    } while ((Get-Date) -lt $deadline)
    return $false
}

$qmpListener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback,0)
$qmpListener.Start()
$qmpPort = ([System.Net.IPEndPoint]$qmpListener.LocalEndpoint).Port
$qmpListener.Stop()
$firmwareArgs = @('-drive',('if=pflash,format=raw,readonly=on,file='+$ovmf))
if ($isTransitionAttribution) { $firmwareArgs += @('-drive',('if=pflash,format=raw,file='+$uefiVarsImage)) }
$qemuArgs = @('-accel','tcg','-machine','pc','-smp','1') + $firmwareArgs + @(
    '-drive',('file=fat:rw:'+$esp+',format=raw,if=ide,index=0'),
    '-m','1024M','-vga','std','-display','none',
    '-serial',('file:'+$serial),
    '-qmp',('tcp:127.0.0.1:{0},server,nowait' -f $qmpPort),
    '-boot','order=c','-no-shutdown','-rtc','base=utc,clock=host')
$qacArgs = @('run','--project','guideXOSServer','--mode','acceptance','--',$qemu) + $qemuArgs
[ordered]@{qacCli=$qacCli;arguments=$qacArgs;qemu=$qemu;qemuArguments=$qemuArgs;qmpPort=$qmpPort;
    candidateRoot=$candidate;esp=$esp;serial=$serial;timeoutSeconds=$BootTimeoutSeconds;
    candidateManifestSha256=$candidateManifestSha256;candidateIntegrityInventory=$CandidateIntegrityInventory;
    candidateIntegrityInventorySha256=$candidateIntegrityInventorySha256;qemuSha256=$qemuSha256;
    candidateProfile=$candidateManifest.mode;gcInitFailureAttributionProfile=$isGcInitFailureAttribution;
    qualificationCorrelationId=$runCorrelationId;candidateEsp=$candidateEsp;isolatedGuestEsp=if($isTransitionAttribution){$esp}else{$null};
    qemuFirmwareCode=$ovmf;qemuFirmwareCodeSha256=$qemuCodeSha256;
    uefiVarsTemplate=$uefiVarsTemplate;uefiVarsTemplateSha256=$uefiVarsTemplateSha256;
    writableUefiVarsImage=$uefiVarsImage;writableUefiVarsImageSha256=if($isTransitionAttribution){$guestState.UefiVarsImageSha256}else{$null};
    writableUefiVarsImageLengthBytes=if($isTransitionAttribution){$guestState.UefiVarsImageLengthBytes}else{$null};
    nativeAllocationGuestAttempt=if($isNativeAllocationFailureAttribution){$NativeAllocationGuestAttempt}else{$null};
    threadStoreAttempt=if($isThreadStoreReadiness){$ThreadStoreAttempt}else{$null};attemptGuardPath=$attemptPath;
    guestBootLimit=1;automaticRetry=$false} |
    ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $runRoot 'qac-run-arguments.json') -Encoding utf8

if ($HostedPreflightOnly) {
    if (-not $isTransitionAttribution) { throw '-HostedPreflightOnly is reserved for the explicit transition qualification profile.' }
    $candidateIntegrityAfterCopy = Test-QacCandidateIntegrityInventory -CandidateRoot $candidate `
        -InventoryPath $CandidateIntegrityInventory
    if ($candidateIntegrityAfterCopy.CandidateManifestSha256 -cne $candidateManifestSha256 -or
        $candidateIntegrityAfterCopy.InventorySha256 -cne $candidateIntegrityInventorySha256) {
        throw 'Candidate integrity changed while preparing isolated guest state.'
    }
    $hostedPreflight = [ordered]@{
        result='TRANSITION_PROFILE_HOSTED_PREFLIGHT_PASS';qualificationCorrelationId=$runCorrelationId
        candidateRoot=$candidate;candidateManifestSha256=$candidateManifestSha256
        candidateIntegrityInventory=$CandidateIntegrityInventory
        candidateIntegrityInventorySha256=$candidateIntegrityInventorySha256
        candidateIntegrityFileCount=$candidateIntegrityAfterCopy.FileCount
        candidateEsp=$candidateEsp;isolatedGuestEsp=$esp;isolatedEspFileCount=$guestState.EspFileCount
        sourceRamdiskSha256=$candidateManifest.sourceRamdiskSha256
        isolatedRamdiskSha256=$guestState.IsolatedRamdiskSha256
        qemuExecutable=$qemu;qemuExecutableSha256=$qemuSha256
        qemuFirmwareCode=$ovmf;qemuFirmwareCodeSha256=$qemuCodeSha256
        uefiVarsTemplate=$uefiVarsTemplate;uefiVarsTemplateSha256=$uefiVarsTemplateSha256
        writableUefiVarsImage=$uefiVarsImage;writableUefiVarsImageSha256=$guestState.UefiVarsImageSha256
        runRoot=$runRoot;serialLog=$serial;qmpLog=$qmpLog;attemptGuardPath=$attemptPath
        qacQueried=$false;qacReserved=$false;qemuLaunched=$false;attemptGuardCreated=$false;guestBoots=0
        qemuArguments=$qemuArgs
    }
    $hostedPreflightPath = Join-Path $runRoot 'hosted-preflight.json'
    $hostedPreflight | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $hostedPreflightPath -Encoding utf8
    Write-Output "C188R12R4R5R2R3_TRANSITION_HOSTED_PREFLIGHT=PASS RUN_ROOT=$runRoot"
    Write-Output "C188R12R4R5R2R3_TRANSITION_HOSTED_PREFLIGHT_MANIFEST=$hostedPreflightPath"
    exit 0
}

if ($isTransitionAttribution) {
    $candidateIntegrityBeforeAdmission = Test-QacCandidateIntegrityInventory -CandidateRoot $candidate `
        -InventoryPath $CandidateIntegrityInventory
    if ($candidateIntegrityBeforeAdmission.CandidateManifestSha256 -cne $candidateManifestSha256 -or
        $candidateIntegrityBeforeAdmission.InventorySha256 -cne $candidateIntegrityInventorySha256) {
        throw 'Candidate or integrity inventory changed before QAC admission.'
    }
}
$before = Get-QacSnapshot 'qac-before'
$beforeWindowsCensus = Get-WindowsQemuProcessCensus 'qac-before'
$beforeInspectionCapture = Get-QacReservationInspections $before 'qac-before'
$preflightPassed = Test-QacClearWithCensus -Snapshot $before `
    -WindowsCensusComplete $beforeWindowsCensus.Complete -LocalQemuProcesses $beforeWindowsCensus.Processes `
    -InspectionsComplete $beforeInspectionCapture.Complete -InspectionCount $beforeInspectionCapture.Count
$qacProcess = $null
$stdoutTask = $null
$stderrTask = $null
$qmpConnected = $false
$admitted = $false
$qacReservationId = $null
$qacExitCode = $null
$qemuStatus = $null
$qemuStatusResponse = $null
$runError = $null
$serialText = ''
$guestPass = $false
$transitionTerminalSeen = $false
$releaseVerified = $false
$during = $null
$duringInspection = $null
$duringInspectionPath = $null
$qacOwnershipIdentityPath = $null
$qacOwnershipIdentity = $null
$localGuestIdentity = $null
$ownedQemuLocalProcess = $null
$after = $null
$qacAfterWindowsCensusPath = $null
$qacAfterInspectionRecords = [System.Collections.Generic.List[object]]::new()
$attemptedUtc = $null

try {
    if (-not $preflightPassed) { throw 'BLOCKED_QAC_PREFLIGHT: coordinator/census is not online, empty, and internally consistent.' }

    # This CreateNew marker permanently prevents a second QAC invocation for this candidate.
    $attemptedUtc = [DateTime]::UtcNow.ToString('o')
    $attempt = [ordered]@{candidateRoot=$candidate;attemptedAtUtc=$attemptedUtc;qualificationCorrelationId=$runCorrelationId;
        candidateEsp=if($isTransitionAttribution){$candidateEsp}else{$null};isolatedGuestEsp=if($isTransitionAttribution){$esp}else{$null};
        qemuFirmwareCode=$ovmf;qemuFirmwareCodeSha256=$qemuCodeSha256;
        uefiVarsTemplate=$uefiVarsTemplate;uefiVarsTemplateSha256=$uefiVarsTemplateSha256;
        writableUefiVarsImage=$uefiVarsImage;writableUefiVarsImageSha256=if($isTransitionAttribution){$guestState.UefiVarsImageSha256}else{$null};
        threadStoreAttempt=if($isThreadStoreReadiness){$ThreadStoreAttempt}else{$null};
        nativeAllocationGuestAttempt=if($isNativeAllocationFailureAttribution){$NativeAllocationGuestAttempt}else{$null};
        candidateProfile=$candidateManifest.mode;gcInitFailureAttributionProfile=$isGcInitFailureAttribution;
        runnerOwnerPid=$PID;runnerOwnerUser=[System.Security.Principal.WindowsIdentity]::GetCurrent().Name;
        runnerScript=$PSCommandPath;runnerScriptSha256=$runnerScriptSha256;
        candidateIntegrityInventory=$CandidateIntegrityInventory;candidateIntegrityInventorySha256=$candidateIntegrityInventorySha256;
        qemuExecutable=$qemu;qemuExecutableSha256=$qemuSha256;
        candidateIdentity=[ordered]@{candidateManifestSha256=Get-Sha256 $candidateManifestPath;
            diagnosticElfSha256=[string]$candidateManifest.managedElfSha256;
            diagnosticKernelSha256=[string]$candidateManifest.specializedKernelSha256;
            ramdiskSha256=[string]$candidateManifest.candidateRamdiskSha256};
        maxGuestBoots=1;automaticRetry=$false;
        candidateManifestSha256=Get-Sha256 $candidateManifestPath;
        qacProject='guideXOSServer';qacMode='acceptance';qemuArguments=$qemuArgs}
    $attemptJson = $attempt | ConvertTo-Json -Depth 6
    $attemptStream = [System.IO.File]::Open($attemptPath,[System.IO.FileMode]::CreateNew,
        [System.IO.FileAccess]::Write,[System.IO.FileShare]::Read)
    try {
        $attemptBytes = [System.Text.Encoding]::UTF8.GetBytes($attemptJson)
        $attemptStream.Write($attemptBytes,0,$attemptBytes.Length)
        $attemptStream.Flush($true)
    } finally { $attemptStream.Dispose() }

    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $qacCli
    $psi.WorkingDirectory = $repo
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    foreach ($argument in $qacArgs) { [void]$psi.ArgumentList.Add([string]$argument) }
    $qacProcess = [System.Diagnostics.Process]::Start($psi)
    if (-not $qacProcess) { throw 'Unable to start the QAC-owned QEMU command.' }
    $stdoutTask = $qacProcess.StandardOutput.ReadToEndAsync()
    $stderrTask = $qacProcess.StandardError.ReadToEndAsync()

    $admissionDeadline = (Get-Date).AddSeconds($AdmissionWindowSeconds)
    $admissionSample = 0
    $ownerUser = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
    do {
        if ($qacProcess.HasExited) { throw "QAC exited before exclusive admission ($($qacProcess.ExitCode))." }
        $admissionSample++
        $prefix = 'qac-during-{0:D3}' -f $admissionSample
        $snapshot = Get-QacSnapshot $prefix
        if ($null -eq $qacDuringStatusPath) {
            $qacDuringStatusPath = $snapshot.StatusPath
            $qacDuringListPath = $snapshot.ListPath
        }
        $listFields = ConvertFrom-QacListSnapshot -Text $snapshot.List -ExitCode $snapshot.ListExit
        $ownerRows = @($listFields.ReservationRows | Where-Object {
            $owner = [regex]::Match($_.Owner, '^(?<user>.+?) \(PID (?<pid>\d+)\)$')
            $owner.Success -and [int]$owner.Groups['pid'].Value -eq $qacProcess.Id -and
                [string]::Equals($owner.Groups['user'].Value,$ownerUser,[System.StringComparison]::OrdinalIgnoreCase)
        })
        if ($ownerRows.Count -eq 1) {
            $qacReservationId = [string]$ownerRows[0].Id
            if ($ownerRows[0].State -ceq 'Running' -and $null -ne $ownerRows[0].ChildProcessId) {
                $inspectionCapture = Get-QacInspection $qacReservationId $prefix
                if ($inspectionCapture.ExitCode -eq 0 -and $null -ne $inspectionCapture.Inspection) {
                    $ownerStartUtc = [DateTimeOffset]$qacProcess.StartTime.ToUniversalTime()
                    $qacOwnedSnapshot = Test-QacRunningOwned -Snapshot $snapshot -ReservationId $qacReservationId `
                        -OwnerPid $qacProcess.Id -OwnerUser $ownerUser -QemuPath $qemu `
                        -Inspection $inspectionCapture.Inspection -ExpectedOwnerStartTimeUtc $ownerStartUtc
                    if ($qacOwnedSnapshot) {
                        $during = $snapshot
                        $qacDuringStatusPath = $snapshot.StatusPath
                        $qacDuringListPath = $snapshot.ListPath
                        $duringInspection = $inspectionCapture.Inspection
                        $duringInspectionPath = $inspectionCapture.Path
                        $localProcess = $null
                        try {
                            $localProcess = Get-CimInstance -ClassName Win32_Process `
                                -Filter ('ProcessId = {0}' -f [int]$inspectionCapture.Inspection.processId) -ErrorAction Stop
                        } catch { $localGuestIdentity = [ordered]@{verificationError=$_.Exception.Message} }
                        if ($null -ne $localProcess -and (Test-QacLocalQemuChildIdentity -LocalProcess $localProcess `
                                -Inspection $inspectionCapture.Inspection -OwnerPid $qacProcess.Id `
                                -QemuPath $qemu -QmpPort $qmpPort)) {
                            $admitted = $true
                            $during = $snapshot
                            $duringInspection = $inspectionCapture.Inspection
                            $duringInspectionPath = $inspectionCapture.Path
                            $ownedQemuLocalProcess = $localProcess
                            $localGuestIdentity = [ordered]@{
                                processId=[int]$localProcess.ProcessId
                                parentProcessId=[int]$localProcess.ParentProcessId
                                executablePath=[string]$localProcess.ExecutablePath
                                creationDate=[string]$localProcess.CreationDate
                                commandLine=[string]$localProcess.CommandLine
                                qmpPort=$qmpPort
                                qacInspectionPath=$inspectionCapture.Path
                            }
                            [System.IO.File]::WriteAllText((Join-Path $runRoot 'qemu-local-child-identity.json'),
                                ($localGuestIdentity | ConvertTo-Json -Depth 5),[System.Text.UTF8Encoding]::new($false))
                            $qacOwnershipIdentityPath = Join-Path $runRoot 'qac-guest-ownership-identity.json'
                            $qacOwnershipIdentity = [ordered]@{
                                candidateManifestSha256=$candidateManifestSha256
                                candidateIntegrityInventory=$CandidateIntegrityInventory
                                candidateIntegrityInventorySha256=$candidateIntegrityInventorySha256
                                guardPath=$attemptPath
                                guardSha256=Get-Sha256 $attemptPath
                                runnerScript=$PSCommandPath
                                runnerScriptSha256=Get-Sha256 $PSCommandPath
                                qemuExecutable=$qemu
                                qemuExecutableSha256=$qemuSha256
                                qacReservationId=$qacReservationId
                                qacOwnerPid=$qacProcess.Id
                                qacOwnerUser=$ownerUser
                                qemuChild=$localGuestIdentity
                            }
                            [System.IO.File]::WriteAllText($qacOwnershipIdentityPath,
                                ($qacOwnershipIdentity | ConvertTo-Json -Depth 7),[System.Text.UTF8Encoding]::new($false))
                            break
                        } elseif ($null -ne $localProcess) {
                            $localGuestIdentity = [ordered]@{
                                verificationError='Local PID, parent, executable, creation time, or QMP command line did not match QAC inspection.'
                                processId=[int]$localProcess.ProcessId
                                parentProcessId=[int]$localProcess.ParentProcessId
                                executablePath=[string]$localProcess.ExecutablePath
                                creationDate=[string]$localProcess.CreationDate
                                qmpPort=$qmpPort
                            }
                        }
                    }
                } else {
                    $duringInspectionPath = $inspectionCapture.Path
                }
            }
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $admissionDeadline)
    if (-not $admitted) { throw 'QAC did not provide a complete, stable, owner-verified exclusive process census inside the bounded admission window.' }

    if (-not (Wait-Qmp 30 $qacProcess)) { throw 'QMP did not become reachable within 30 seconds of QAC admission.' }
    $qmpConnected = $true
    $qemuStatusResponse = Invoke-Qmp 'query-status'
    $qemuStatus = ConvertFrom-QmpQueryStatusResponse -Text $qemuStatusResponse
    if (-not $qemuStatus.Valid) {
        throw 'QMP query-status response was missing the required status or Boolean running fields.'
    }
    if (-not $qemuStatus.running) { throw 'QEMU query-status did not report a running guest process.' }
    $deadline = (Get-Date).AddSeconds($BootTimeoutSeconds)
    do {
        $serialText = Read-GuestSerial
        if ($isTransitionAttribution) {
            $transitionTerminalSeen = $serialText -match '(?m)^\[C188R12R4R5R2R3_TRANSITION\] ' -or
                $serialText -match '(?m)^\[C188R12R4R5R2R3_FATAL\] ' -or
                $serialText -match '(?m)^\[nativeaot-pal-qemu-test\] FAIL_FAST\b'
            $guestPass = $transitionTerminalSeen
        } elseif ($isSafeHandoff) {
            $guestPass = $serialText -match '(?m)^\[nativeaot-safe-handoff\] status=intentionally_stopped initialization=accepted moduleRegistration=accepted managedTransition=not_attempted managedEntry=not_reached runtimeError=none\s*$' -and
                $serialText -match '(?m)^\[C188R12R4R5R1_INIT\] trace_end .*overflow=00000000 serialDropped=00000000 adapterStatus=00000000\s*$' -and
                $serialText -notmatch '(?m)^\[C188R12R4R5R4_THREAD\].*event=(?:module_initialize_enter|transition_enter|managed_dispatch|managed_entry)\s+' -and
                $serialText -notmatch '(?m)^\[nativeaot-pal-qemu-test\] FAIL_FAST\b'
            $safeHandoffTerminal = $serialText -match '(?m)^\[nativeaot-safe-handoff\] (?:status=intentionally_stopped|diagnosticError=)'
        } elseif ($isNativeAllocationFailureAttribution) {
            $guestPass = (Test-NativeAllocationTraceComplete $serialText) -and
                $serialText -match '(?m)^\[C188R12R4R5R1_INIT\] trace_end .*?(?:rhResult|adapterStatus)=(?:0x)?[0-9A-F]{8}\s*$' -and
                $serialText -match '(?m)^\[C188R12R4R5R1_INIT\] managed_main_gate=blocked\b'
        } elseif ($isGcInitFailureAttribution) {
            $r4FailureBoundary = $serialText -match '(?m)^\[C188R12R4R5R4_THREAD\].*event=(?:managed_entry_blocked_init_failure|transition_attach_incomplete)\s+' -or
                $serialText -match '(?m)^\[C188R12R4R5R4_THREAD\].*event=pal_stack_current\s+raw0=[0-9A-Fa-f]{16}\s+raw1=(?!0{16}\s)[0-9A-Fa-f]{16}\s*$'
            $r4GcTerminal = $serialText -match '(?m)^\[C188R12R4R5-GC-OWNERSHIP\] outcome=(?:PASS|FAIL) callbacks=\S+ result1=\S+ result2=\S+ result3=\S+\s*$'
            $guestPass = $serialText -match '(?m)^\[C188R12R4R5R4_THREAD\].*event=rhinitialize_return\s+' -and
                ($r4FailureBoundary -or $r4GcTerminal)
        } elseif ($isInitAttribution) {
            $guestPass = $serialText -match '(?m)^\[C188R12R4R5R1_INIT\] trace_end .*rhResult=(?:0x)?[0-9A-F]{8}\s*$' -and
                $serialText -match '(?m)^\[C188R12R4R5R1_INIT\] managed_main_gate=blocked\b'
        } elseif ($isInitReturnContract) {
            $guestPass = $serialText -match '(?m)^\[C188R12R4R5R1_INIT\] trace_end .*adapterStatus=(?:0x)?[0-9A-F]{8}\s*$' -and
                $serialText -match '(?m)^\[C188R12R4R5R3_INIT\] init_result nativeBool=(?:0x)?[0-9A-F]{8} adapterStatus=(?:0x)?[0-9A-F]{8} nativeOutcome=(?:success|failure|unknown) adapterOutcome=(?:success|failure)\s*$' -and
                $serialText -match '(?m)^\[C188R12R4R5R1_INIT\] managed_main_gate=blocked\b'
        } elseif ($isThreadStoreReadiness) {
            $r4FailureBoundary = $serialText -match '(?m)^\[C188R12R4R5R4_THREAD\].*event=(?:managed_entry_blocked_init_failure|transition_attach_incomplete)\s+' -or
                $serialText -match '(?m)^\[C188R12R4R5R4_THREAD\].*event=pal_stack_current\s+raw0=[0-9A-Fa-f]{16}\s+raw1=(?!0{16}\s)[0-9A-Fa-f]{16}\s*$'
            $r4GcTerminal = $serialText -match '(?m)^\[C188R12R4R5-GC-OWNERSHIP\] outcome=(?:PASS|FAIL) callbacks=\S+ result1=\S+ result2=\S+ result3=\S+\s*$'
            $guestPass = $serialText -match '(?m)^\[C188R12R4R5R4_THREAD\].*event=rhinitialize_return\s+' -and
                ($r4FailureBoundary -or $r4GcTerminal)
        } else {
            $guestPass = $serialText -match '(?m)^\[C188R12R4R5-GC-OWNERSHIP\] outcome=PASS callbacks=(?:0x)?00000003 result1=(?:0x)?00000000 result2=(?:0x)?00000000 result3=(?:0x)?00000000\s*$'
        }
        if ($guestPass -or ($isSafeHandoff -and $safeHandoffTerminal)) { break }
        if ($qacProcess.HasExited) { break }
        Start-Sleep -Milliseconds 200
    } while ((Get-Date) -lt $deadline)
    $serialText = Read-GuestSerial
$safeHandoffTerminal = $false
if ($isTransitionAttribution) {
    $transitionTerminalSeen = $serialText -match '(?m)^\[C188R12R4R5R2R3_TRANSITION\] ' -or
        $serialText -match '(?m)^\[C188R12R4R5R2R3_FATAL\] ' -or
        $serialText -match '(?m)^\[nativeaot-pal-qemu-test\] FAIL_FAST\b'
    $guestPass = $transitionTerminalSeen
} elseif ($isSafeHandoff) {
        $guestPass = $serialText -match '(?m)^\[nativeaot-safe-handoff\] status=intentionally_stopped initialization=accepted moduleRegistration=accepted managedTransition=not_attempted managedEntry=not_reached runtimeError=none\s*$' -and
            $serialText -match '(?m)^\[C188R12R4R5R1_INIT\] trace_end .*overflow=00000000 serialDropped=00000000 adapterStatus=00000000\s*$' -and
            $serialText -notmatch '(?m)^\[C188R12R4R5R4_THREAD\].*event=(?:module_initialize_enter|transition_enter|managed_dispatch|managed_entry)\s+' -and
            $serialText -notmatch '(?m)^\[nativeaot-pal-qemu-test\] FAIL_FAST\b'
    $safeHandoffTerminal = $serialText -match '(?m)^\[nativeaot-safe-handoff\] (?:status=intentionally_stopped|diagnosticError=)'
    } elseif ($isNativeAllocationFailureAttribution) {
        $guestPass = (Test-NativeAllocationTraceComplete $serialText) -and
            $serialText -match '(?m)^\[C188R12R4R5R1_INIT\] trace_end .*?(?:rhResult|adapterStatus)=(?:0x)?[0-9A-F]{8}\s*$' -and
            $serialText -match '(?m)^\[C188R12R4R5R1_INIT\] managed_main_gate=blocked\b'
    } elseif ($isGcInitFailureAttribution) {
        $r4FailureBoundary = $serialText -match '(?m)^\[C188R12R4R5R4_THREAD\].*event=(?:managed_entry_blocked_init_failure|transition_attach_incomplete)\s+' -or
            $serialText -match '(?m)^\[C188R12R4R5R4_THREAD\].*event=pal_stack_current\s+raw0=[0-9A-Fa-f]{16}\s+raw1=(?!0{16}\s)[0-9A-Fa-f]{16}\s*$'
        $r4GcTerminal = $serialText -match '(?m)^\[C188R12R4R5-GC-OWNERSHIP\] outcome=(?:PASS|FAIL) callbacks=\S+ result1=\S+ result2=\S+ result3=\S+\s*$'
        $guestPass = $serialText -match '(?m)^\[C188R12R4R5R4_THREAD\].*event=rhinitialize_return\s+' -and
            ($r4FailureBoundary -or $r4GcTerminal)
    } elseif ($isInitAttribution) {
        $guestPass = $serialText -match '(?m)^\[C188R12R4R5R1_INIT\] trace_end .*rhResult=(?:0x)?[0-9A-F]{8}\s*$' -and
            $serialText -match '(?m)^\[C188R12R4R5R1_INIT\] managed_main_gate=blocked\b'
    } elseif ($isInitReturnContract) {
        $guestPass = $serialText -match '(?m)^\[C188R12R4R5R1_INIT\] trace_end .*adapterStatus=(?:0x)?[0-9A-F]{8}\s*$' -and
            $serialText -match '(?m)^\[C188R12R4R5R3_INIT\] init_result nativeBool=(?:0x)?[0-9A-F]{8} adapterStatus=(?:0x)?[0-9A-F]{8} nativeOutcome=(?:success|failure|unknown) adapterOutcome=(?:success|failure)\s*$' -and
            $serialText -match '(?m)^\[C188R12R4R5R1_INIT\] managed_main_gate=blocked\b'
    } elseif ($isThreadStoreReadiness) {
        $r4FailureBoundary = $serialText -match '(?m)^\[C188R12R4R5R4_THREAD\].*event=(?:managed_entry_blocked_init_failure|transition_attach_incomplete)\s+' -or
            $serialText -match '(?m)^\[C188R12R4R5R4_THREAD\].*event=pal_stack_current\s+raw0=[0-9A-Fa-f]{16}\s+raw1=(?!0{16}\s)[0-9A-Fa-f]{16}\s*$'
        $r4GcTerminal = $serialText -match '(?m)^\[C188R12R4R5-GC-OWNERSHIP\] outcome=(?:PASS|FAIL) callbacks=\S+ result1=\S+ result2=\S+ result3=\S+\s*$'
        $guestPass = $serialText -match '(?m)^\[C188R12R4R5R4_THREAD\].*event=rhinitialize_return\s+' -and
            ($r4FailureBoundary -or $r4GcTerminal)
    } else {
        $guestPass = $serialText -match '(?m)^\[C188R12R4R5-GC-OWNERSHIP\] outcome=PASS callbacks=(?:0x)?00000003 result1=(?:0x)?00000000 result2=(?:0x)?00000000 result3=(?:0x)?00000000\s*$'
    }
    try {
        $qemuStatusResponse = Invoke-Qmp 'query-status'
        $qemuStatus = ConvertFrom-QmpQueryStatusResponse -Text $qemuStatusResponse
        if (-not $qemuStatus.Valid) {
            throw 'QMP query-status response was missing the required status or Boolean running fields.'
        }
    } catch { $qemuStatus = [pscustomobject]@{Valid=$false;Status=$null;Running=$null;Error=$_.Exception.Message} }
    if (-not $qacProcess.HasExited) {
        $currentLocalProcess = $null
        try {
            $currentLocalProcess = Get-CimInstance -ClassName Win32_Process `
                -Filter ('ProcessId = {0}' -f [int]$duringInspection.processId) -ErrorAction Stop
        } catch { }
        if ($null -eq $currentLocalProcess -or -not (Test-QacLocalQemuChildIdentity `
                -LocalProcess $currentLocalProcess -Inspection $duringInspection -OwnerPid $qacProcess.Id `
                -QemuPath $qemu -QmpPort $qmpPort)) {
            throw 'Refusing QMP shutdown because the local QEMU child identity is no longer verified.'
        }
        [void](Invoke-Qmp 'quit')
    }
    if (-not $qacProcess.WaitForExit(90000)) { throw 'QAC did not exit after the owned QMP quit request.' }
    $qacExitCode = $qacProcess.ExitCode
} catch {
    $runError = $_.Exception.Message
    if ($null -ne $qacProcess -and -not $qacProcess.HasExited) {
        if ($null -ne $duringInspection) {
            try {
                $ownedQemuLocalProcess = Get-CimInstance -ClassName Win32_Process `
                    -Filter ('ProcessId = {0}' -f [int]$duringInspection.processId) -ErrorAction Stop
            } catch { $ownedQemuLocalProcess = $null }
        }
        if ($null -ne $duringInspection -and $null -ne $ownedQemuLocalProcess -and
            (Test-QacLocalQemuChildIdentity -LocalProcess $ownedQemuLocalProcess -Inspection $duringInspection `
                -OwnerPid $qacProcess.Id -QemuPath $qemu -QmpPort $qmpPort)) {
            if (-not $qmpConnected -and (Wait-Qmp 10 $qacProcess)) { $qmpConnected = $true }
            if ($qmpConnected -and -not $qacProcess.HasExited) {
                try { [void](Invoke-Qmp 'query-status') } catch { }
                try { [void](Invoke-Qmp 'quit') } catch { }
                [void]$qacProcess.WaitForExit(90000)
            }
        } else {
            $latestListLines = & $qacCli list 2>&1
            $latestListExit = $LASTEXITCODE
            $latestList = ($latestListLines | Out-String)
            $cancelAllowed = $false
            $latestFields = ConvertFrom-QacListSnapshot -Text $latestList -ExitCode $latestListExit
            $ownerRows = @($latestFields.ReservationRows | Where-Object {
                $owner = [regex]::Match($_.Owner, '^(?<user>.+?) \(PID (?<pid>\d+)\)$')
                $owner.Success -and [int]$owner.Groups['pid'].Value -eq $qacProcess.Id -and
                    [string]::Equals($owner.Groups['user'].Value,[System.Security.Principal.WindowsIdentity]::GetCurrent().Name,
                        [System.StringComparison]::OrdinalIgnoreCase)
            })
            if ($ownerRows.Count -eq 1 -and $ownerRows[0].State -in @('Waiting','Blocked')) {
                $qacReservationId = [string]$ownerRows[0].Id
                $cancelAllowed = $true
            }
            if ($cancelAllowed) {
                $cancelOutput = (& $qacCli cancel $qacReservationId 2>&1 | Out-String)
                $cancelExit = $LASTEXITCODE
                $cancelLog = Join-Path $runRoot 'qac-cancel.log'
                if (Test-Path -LiteralPath $cancelLog) { throw "Refusing to overwrite QAC cancel evidence: $cancelLog" }
                [System.IO.File]::WriteAllText($cancelLog,$cancelOutput,[System.Text.UTF8Encoding]::new($false))
                if ($cancelExit -eq 0) { [void]$qacProcess.WaitForExit(90000) }
            }
        }
    }
} finally {
    if ($null -ne $qacProcess) {
        if ($qacProcess.HasExited) { $qacExitCode = $qacProcess.ExitCode }
        if ($null -ne $stdoutTask -and $stdoutTask.IsCompleted) {
            Set-Content -LiteralPath (Join-Path $runRoot 'qac.stdout.log') -Value $stdoutTask.Result -Encoding utf8
        }
        if ($null -ne $stderrTask -and $stderrTask.IsCompleted) {
            Set-Content -LiteralPath (Join-Path $runRoot 'qac.stderr.log') -Value $stderrTask.Result -Encoding utf8
        }
    }
    $afterDeadline = (Get-Date).AddSeconds(30)
    do {
        $afterSample = if ($null -eq $after) { 1 } else { $afterSample + 1 }
        $after = Get-QacSnapshot ('qac-after-{0:D3}' -f $afterSample)
        $afterWindowsCensus = Get-WindowsQemuProcessCensus ('qac-after-{0:D3}' -f $afterSample)
        $qacAfterWindowsCensusPath = $afterWindowsCensus.Path
        $afterInspectionCapture = Get-QacReservationInspections $after ('qac-after-{0:D3}' -f $afterSample)
        foreach ($record in $afterInspectionCapture.Records) { $qacAfterInspectionRecords.Add($record) }
        $qacAfterStatusPath = $after.StatusPath
        $qacAfterListPath = $after.ListPath
        if (Test-QacClearWithCensus -Snapshot $after `
                -WindowsCensusComplete $afterWindowsCensus.Complete -LocalQemuProcesses $afterWindowsCensus.Processes `
                -InspectionsComplete $afterInspectionCapture.Complete -InspectionCount $afterInspectionCapture.Count) {
            $releaseVerified = $true; break
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $afterDeadline)
}

$serialText = Read-GuestSerial
$serialAnalysisPath = Join-Path $runRoot 'serial-analysis.json'
$serialAnalyzer = if ($isTransitionAttribution) {
    Join-Path $PSScriptRoot 'analyze-c188r12r4r5r2r3-transition-attribution.ps1'
} elseif ($isSafeHandoff) {
    Join-Path $PSScriptRoot 'analyze-c188r12r4r5r2r2r4-safe-handoff.ps1'
} elseif ($isThreadStoreReadiness) {
    Join-Path $PSScriptRoot 'analyze-c188r12r4r5r4-threadstore-readiness.ps1'
} else { Join-Path $PSScriptRoot 'analyze-c188r12r4r5r1-init-serial.ps1' }
$serialSha256BeforeAnalysis = if (Test-Path -LiteralPath $serial) { Get-Sha256 $serial } else { $null }
$serialAnalysis = $null
$transitionAttributionAnalysisPath = if ($isTransitionAttribution) { Join-Path $runRoot 'transition-attribution-analysis.json' } else { $null }
$transitionAttributionAnalysis = $null
$transitionAttributionAnalysisExitCode = $null
$transitionAttributionAnalysisError = $null
$threadStoreAnalysisPath = $null
$threadStoreAnalysis = $null
$threadStoreAnalysisExitCode = $null
$serialAnalysisError = $null
$serialAnalysisExitCode = $null
$gcInitSourceAnalysisPath = $null
$gcInitSourceAnalysis = $null
$gcInitSourceAnalysisExitCode = $null
$semiSharedAnalysisPath = $null
$semiSharedAnalysis = $null
$semiSharedAnalysisExitCode = $null
$semiSharedRecordCaptureComplete = $false
$nativeAllocationAnalysisPath = $null
$nativeAllocationAnalysis = $null
$nativeAllocationAnalysisExitCode = $null
$nativeAllocationAnalysisError = $null
$nativeAllocationRecordCaptureComplete = $false
$nativeAllocationRecordPath = $null
$gcInitRecordPath = $null
$semiSharedRecordPath = $null
if ($isNativeAllocationFailureAttribution -and (Test-Path -LiteralPath $serial -PathType Leaf)) {
    $serialLines = @($serialText -split "`r?`n")
    $nativeAllocationRecordPath = Join-Path $runRoot 'native-allocation-diagnostic-record.raw.txt'
    $gcInitRecordPath = Join-Path $runRoot 'gc-init-diagnostic-record.raw.txt'
    $semiSharedRecordPath = Join-Path $runRoot 'semishared-diagnostic-record.raw.txt'
    $recordGroups = @(
        [pscustomobject]@{path=$nativeAllocationRecordPath;pattern='^\[C188R12R4R5R4R5R2R2_NATIVE_ALLOC\] '},
        [pscustomobject]@{path=$gcInitRecordPath;pattern='^\[C188R12R4R5R4R5_GCINIT\] '},
        [pscustomobject]@{path=$semiSharedRecordPath;pattern='^\[C188R12R4R5R4R5R2_SEMISHARED\] '}
    )
    foreach ($group in $recordGroups) {
        if (Test-Path -LiteralPath $group.path) { throw "Refusing to overwrite derived serial record: $($group.path)" }
        $lines = [string[]]@($serialLines | Where-Object { $_ -match $group.pattern })
        [System.IO.File]::WriteAllLines($group.path,$lines,[System.Text.UTF8Encoding]::new($false))
    }
}
if (Test-Path -LiteralPath $serial -PathType Leaf) {
    try {
        if ($isTransitionAttribution) {
            & pwsh -NoProfile -ExecutionPolicy Bypass -File $serialAnalyzer `
                -SerialPath $serial -CallSiteMapPath ([string]$candidateManifest.transitionCallSiteMap) `
                -OutputPath $transitionAttributionAnalysisPath | Out-Null
            $transitionAttributionAnalysisExitCode = $LASTEXITCODE
            if ($transitionAttributionAnalysisExitCode -eq 0 -and
                (Test-Path -LiteralPath $transitionAttributionAnalysisPath -PathType Leaf)) {
                $transitionAttributionAnalysis = Get-Content -LiteralPath $transitionAttributionAnalysisPath -Raw | ConvertFrom-Json
            } else {
                $transitionAttributionAnalysisError = "Transition analyzer exited $transitionAttributionAnalysisExitCode."
            }
        } else {
            $analysisArgs = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$serialAnalyzer,
                '-SerialPath',$serial,'-OutputPath',$serialAnalysisPath)
            if (-not $isThreadStoreReadiness -and -not $isSafeHandoff -and ($runError -match 'timeout|did not exit|not exit' -or
                ($null -ne $qacProcess -and -not $qacProcess.HasExited))) { $analysisArgs += '-GuestTimedOut' }
            & pwsh @analysisArgs | Out-Null
            $serialAnalysisExitCode = $LASTEXITCODE
            if ($serialAnalysisExitCode -eq 0 -and (Test-Path -LiteralPath $serialAnalysisPath -PathType Leaf)) {
                $serialAnalysis = Get-Content -LiteralPath $serialAnalysisPath -Raw | ConvertFrom-Json
            } else {
                $serialAnalysisError = "Analyzer exited $serialAnalysisExitCode."
            }
        }
    } catch {
        if ($isTransitionAttribution) { $transitionAttributionAnalysisError = $_.Exception.Message }
        else { $serialAnalysisError = $_.Exception.Message }
    }
} else {
    if ($isTransitionAttribution) { $transitionAttributionAnalysisError = 'Raw serial file is missing.' }
    else { $serialAnalysisError = 'Raw serial file is missing.' }
}
if ($isGcInitFailureAttribution -and -not $isSafeHandoff -and (Test-Path -LiteralPath $serial -PathType Leaf)) {
    $threadStoreAnalysisPath = Join-Path $runRoot 'serial-threadstore-analysis.json'
    try {
        & pwsh -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'analyze-c188r12r4r5r4-threadstore-readiness.ps1') `
            -SerialPath $serial -OutputPath $threadStoreAnalysisPath | Out-Null
        $threadStoreAnalysisExitCode = $LASTEXITCODE
        if ($threadStoreAnalysisExitCode -eq 0 -and (Test-Path -LiteralPath $threadStoreAnalysisPath -PathType Leaf)) {
            $threadStoreAnalysis = Get-Content -LiteralPath $threadStoreAnalysisPath -Raw | ConvertFrom-Json
        } elseif ($null -eq $serialAnalysisError) {
            $serialAnalysisError = "ThreadStore analyzer exited $threadStoreAnalysisExitCode."
        }
    } catch {
        if ($null -eq $serialAnalysisError) { $serialAnalysisError = 'ThreadStore analyzer: ' + $_.Exception.Message }
    }
}
if ($isSafeHandoff -and (Test-Path -LiteralPath $serial -PathType Leaf)) {
    $gcInitSourceAnalysisPath = Join-Path $runRoot 'serial-gc-init-source-analysis.json'
    try {
        & pwsh -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'analyze-c188r12r4r5r1-init-serial.ps1') `
            -SerialPath $serial -OutputPath $gcInitSourceAnalysisPath | Out-Null
        $gcInitSourceAnalysisExitCode = $LASTEXITCODE
        if ($gcInitSourceAnalysisExitCode -eq 0 -and (Test-Path -LiteralPath $gcInitSourceAnalysisPath -PathType Leaf)) {
            $gcInitSourceAnalysis = Get-Content -LiteralPath $gcInitSourceAnalysisPath -Raw | ConvertFrom-Json
        } elseif ($null -eq $serialAnalysisError) {
            $serialAnalysisError = "GC initialization source analyzer exited $gcInitSourceAnalysisExitCode."
        }
    } catch {
        if ($null -eq $serialAnalysisError) { $serialAnalysisError = 'GC initialization source analyzer: ' + $_.Exception.Message }
    }
}
if (($isSemiSharedAttribution -or $isNativeAllocationFailureAttribution) -and (Test-Path -LiteralPath $serial -PathType Leaf)) {
    $semiSharedAnalysisPath = Join-Path $runRoot 'serial-semishared-analysis.json'
    try {
        & pwsh -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'analyze-c188r12r4r5r4r5r2-semishared.ps1') `
            -SerialPath $serial -OutputPath $semiSharedAnalysisPath `
            -RecordHeaderPath (Join-Path $repo 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_semishared_record.h') `
            -OperationCapacity ([int]$candidateManifest.c188R12R4R5R4R5R2OperationCapacity) | Out-Null
        $semiSharedAnalysisExitCode = $LASTEXITCODE
        if ($semiSharedAnalysisExitCode -eq 0 -and (Test-Path -LiteralPath $semiSharedAnalysisPath -PathType Leaf)) {
            $semiSharedAnalysis = Get-Content -LiteralPath $semiSharedAnalysisPath -Raw | ConvertFrom-Json
        } elseif ($null -eq $serialAnalysisError) {
            $serialAnalysisError = "Semi-shared analyzer exited $semiSharedAnalysisExitCode."
        }
    } catch {
        if ($null -eq $serialAnalysisError) { $serialAnalysisError = 'Semi-shared analyzer: ' + $_.Exception.Message }
    }
}
if ($isNativeAllocationFailureAttribution -and (Test-Path -LiteralPath $serial -PathType Leaf)) {
    $nativeAllocationAnalysisPath = Join-Path $runRoot 'native-allocation-analysis.json'
    try {
        & pwsh -NoProfile -ExecutionPolicy Bypass -File ([string]$candidateManifest.nativeAllocationAnalyzer) `
            -SerialPath $serial -OutputPath $nativeAllocationAnalysisPath -EvidenceKind guest_serial | Out-Null
        $nativeAllocationAnalysisExitCode = $LASTEXITCODE
        if ($nativeAllocationAnalysisExitCode -eq 0 -and (Test-Path -LiteralPath $nativeAllocationAnalysisPath -PathType Leaf)) {
            $nativeAllocationAnalysis = Get-Content -LiteralPath $nativeAllocationAnalysisPath -Raw | ConvertFrom-Json
            $nativeClassifications = @($nativeAllocationAnalysis.results | ForEach-Object { [string]$_.classification })
            $nativeAllocationRecordCaptureComplete = [string]$nativeAllocationAnalysis.evidenceKind -ceq 'guest_serial' -and
                $nativeClassifications.Count -eq 1 -and $nativeClassifications[0] -notmatch '^INCOMPLETE_'
        } else {
            $nativeAllocationAnalysisError = "Native-allocation analyzer exited $nativeAllocationAnalysisExitCode."
        }
    } catch { $nativeAllocationAnalysisError = $_.Exception.Message }
}
$serialSha256AfterAnalysis = if (Test-Path -LiteralPath $serial) { Get-Sha256 $serial } else { $null }
$rawSerialPreservedByAnalysis = $null -ne $serialSha256BeforeAnalysis -and
    $serialSha256BeforeAnalysis -ceq $serialSha256AfterAnalysis
$transitionAttributionCaptureComplete = $false
$transitionAttributionClassification = if ($null -ne $transitionAttributionAnalysis) { [string]$transitionAttributionAnalysis.classification } else { $null }
if ($isTransitionAttribution) {
    $recognizedTransitionClass = $transitionAttributionClassification -in @(
        'TRANSITION_GATE_BLOCKED_SAFE_STOP','RUNTIME_FAIL_FAST_ATTRIBUTED',
        'RUNTIME_FAIL_FAST_CALLSITE_UNRESOLVED','INITIALIZEMODULES_RETURNED_MANAGED_ENTRY_NOT_REQUESTED')
    $transitionAttributionCaptureComplete = $recognizedTransitionClass -and
        [string]$transitionAttributionAnalysis.recordIntegrity -ceq '1' -and
        [string]::IsNullOrWhiteSpace([string]$transitionAttributionAnalysis.parseError) -and
        [bool]$transitionAttributionAnalysis.evidencePreserved -and
        [string]$transitionAttributionAnalysis.callSiteMap.sha256 -ceq [string]$candidateManifest.transitionCallSiteMapSha256 -and
        [bool]$rawSerialPreservedByAnalysis
    $guestPass = $transitionAttributionCaptureComplete
    $initCaptureComplete = $transitionAttributionCaptureComplete
} elseif ($isSafeHandoff) {
    $initCaptureComplete = $null -ne $serialAnalysis -and [bool]$serialAnalysis.diagnosticEvidenceComplete
} else {
    $initCaptureComplete = $null -ne $serialAnalysis -and -not $isThreadStoreReadiness -and [bool]$serialAnalysis.initTraceComplete
}
if ($isSafeHandoff) {
    $gcInternal = if ($null -ne $gcInitSourceAnalysis) { $gcInitSourceAnalysis.gcInitializationInternal } else { $null }
    $gcInitRecordCaptureComplete = $null -ne $gcInternal -and [bool]$gcInternal.traceComplete -and
        [int]$gcInternal.recordCount -eq 16 -and [int]$gcInternal.malformedMarkerCount -eq 0 -and
        [string]$gcInternal.outcome -eq 'gc_heap_initialize_succeeded'
    $semiSharedRecordCaptureComplete = $null -ne $semiSharedAnalysis -and
        [string]$semiSharedAnalysis.outcome -eq 'init_semi_shared_succeeded' -and
        @($semiSharedAnalysis.issues).Count -eq 0
    $initializationAndRegistrationAccepted = $null -ne $serialAnalysis -and
        [bool]$serialAnalysis.initializationAccepted -and [bool]$serialAnalysis.moduleRegistrationAccepted
    $intentionalStopCaptured = $null -ne $serialAnalysis -and
        [string]$serialAnalysis.result -eq 'SAFE_HANDOFF_INTENTIONALLY_STOPPED' -and
        [bool]$serialAnalysis.managedTransitionIntentionallyNotAttempted -and
        -not [bool]$serialAnalysis.managedTransitionAttempted -and
        -not [bool]$serialAnalysis.managedEntryReached
    $guestPass = $null -ne $serialAnalysis -and [bool]$serialAnalysis.diagnosticEvidenceComplete -and
        $initializationAndRegistrationAccepted -and $intentionalStopCaptured -and
        $gcInitRecordCaptureComplete -and $semiSharedRecordCaptureComplete -and
        $nativeAllocationRecordCaptureComplete
    $initCaptureComplete = $guestPass
} elseif ($isGcInitFailureAttribution) {
    $gcInternal = $null
    if ($null -ne $serialAnalysis -and $null -ne $serialAnalysis.PSObject.Properties['gcInitializationInternal']) {
        $gcInternal = $serialAnalysis.gcInitializationInternal
    }
    $gcInitRecordCaptureComplete = $null -ne $gcInternal -and [bool]$gcInternal.traceComplete -and
        [int]$gcInternal.recordCount -eq 16 -and [int]$gcInternal.malformedMarkerCount -eq 0
    $r4GcResult = [regex]::Match($serialText,
        '(?m)^\[C188R12R4R5-GC-OWNERSHIP\] outcome=(PASS|FAIL) callbacks=(\S+) result1=(\S+) result2=(\S+) result3=(\S+)\s*$')
    $threadStoreOutcome = if ($null -ne $threadStoreAnalysis) { [string]$threadStoreAnalysis.outcome } else { '' }
    $threadStoreFailureOutcome = $threadStoreOutcome -in @(
        'INIT_FAILURE_BLOCKED','CURRENT_THREAD_TLS_INCOMPLETE','THREADSTORE_OBSERVATION_INCOMPLETE',
        'THREADSTORE_STATE_INVALID','MODULE_REGISTRATION_INCOMPLETE','MODULE_INITIALIZATION_INCOMPLETE',
        'ATTACHMENT_INCOMPLETE','PAL_ATTACHMENT_INCOMPLETE','PAL_THREAD_CONSTRUCTION_INCOMPLETE',
        'PAL_STACK_QUERY_FAILED','PAL_STACK_BOUNDS_INVALID','MANAGED_ENTRY_INCOMPLETE')
    $threadStoreHandoffAndProofCaptured = $null -ne $threadStoreAnalysis -and
        $threadStoreOutcome -eq 'THREADSTORE_HANDOFF_TRACE_COMPLETE' -and $r4GcResult.Success
    $threadStoreTerminalCaptured = $null -ne $threadStoreAnalysis -and
        [bool]$threadStoreAnalysis.rhInitializeAdapterMappingConsistent -and
        ($threadStoreFailureOutcome -or $threadStoreHandoffAndProofCaptured)
    $guestPass = $gcInitRecordCaptureComplete -and $threadStoreTerminalCaptured
    $initCaptureComplete = $gcInitRecordCaptureComplete
    if ($isSemiSharedAttribution -or $isNativeAllocationFailureAttribution) {
        $semiSharedRecordCaptureComplete = $null -ne $semiSharedAnalysis -and
            [string]$semiSharedAnalysis.outcome -in @('init_semi_shared_succeeded','failure_branch_attributed') -and
            @($semiSharedAnalysis.issues).Count -eq 0
        $guestPass = $guestPass -and $semiSharedRecordCaptureComplete
        $initCaptureComplete = $initCaptureComplete -and $semiSharedRecordCaptureComplete
    }
    if ($isNativeAllocationFailureAttribution) {
        $guestPass = $guestPass -and $nativeAllocationRecordCaptureComplete
        $initCaptureComplete = $initCaptureComplete -and $nativeAllocationRecordCaptureComplete
    }
} elseif ($isInitAttribution) {
    $guestPass = $initCaptureComplete -and $serialAnalysis.rhInitializeResult -in @('success','failure') -and
        @($serialAnalysis.managedMainGateLines | Where-Object { $_ -match 'managed_main_gate=blocked' }).Count -gt 0
} elseif ($isInitReturnContract) {
    $guestPass = $initCaptureComplete -and
        $serialAnalysis.nativeRhInitializeBoolean -in @('00000000','00000001') -and
        [bool]$serialAnalysis.explicitReturnMappingConsistent -and
        [bool]$serialAnalysis.explicitReturnMarkerConsistent -and
        [bool]$serialAnalysis.returnStatusMarkerConsistent -and
        @($serialAnalysis.managedMainGateLines | Where-Object { $_ -match 'managed_main_gate=blocked' }).Count -gt 0
} elseif ($isThreadStoreReadiness) {
    $r4GcResult = [regex]::Match($serialText,
        '(?m)^\[C188R12R4R5-GC-OWNERSHIP\] outcome=(PASS|FAIL) callbacks=(\S+) result1=(\S+) result2=(\S+) result3=(\S+)\s*$')
    $threadStoreOutcome = if ($null -ne $serialAnalysis) { [string]$serialAnalysis.outcome } else { '' }
    $threadStoreFailureOutcome = $threadStoreOutcome -in @(
        'INIT_FAILURE_BLOCKED','CURRENT_THREAD_TLS_INCOMPLETE','THREADSTORE_OBSERVATION_INCOMPLETE',
        'THREADSTORE_STATE_INVALID','MODULE_REGISTRATION_INCOMPLETE','MODULE_INITIALIZATION_INCOMPLETE',
        'ATTACHMENT_INCOMPLETE','PAL_ATTACHMENT_INCOMPLETE','PAL_THREAD_CONSTRUCTION_INCOMPLETE',
        'PAL_STACK_QUERY_FAILED','PAL_STACK_BOUNDS_INVALID','MANAGED_ENTRY_INCOMPLETE')
    $threadStoreHandoffAndProofCaptured = $null -ne $serialAnalysis -and
        $threadStoreOutcome -eq 'THREADSTORE_HANDOFF_TRACE_COMPLETE' -and $r4GcResult.Success
    $guestPass = $null -ne $serialAnalysis -and
        [bool]$serialAnalysis.rhInitializeAdapterMappingConsistent -and
        ($threadStoreFailureOutcome -or $threadStoreHandoffAndProofCaptured)
    $initCaptureComplete = $guestPass
}
$candidateIntegrityAfterGuest = $null
$candidateIntegrityPreservedAfterGuest = $null
$candidateIntegrityPostRunError = $null
$qemuFirmwareCodePreserved = $null
$uefiVarsTemplatePreserved = $null
$uefiVarsImageFinalSha256 = $null
if ($isTransitionAttribution) {
    try {
        $candidateIntegrityAfterGuest = Test-QacCandidateIntegrityInventory -CandidateRoot $candidate `
            -InventoryPath $CandidateIntegrityInventory
        $candidateIntegrityPreservedAfterGuest =
            $candidateIntegrityAfterGuest.CandidateManifestSha256 -ceq $candidateManifestSha256 -and
            $candidateIntegrityAfterGuest.InventorySha256 -ceq $candidateIntegrityInventorySha256
        if (-not $candidateIntegrityPreservedAfterGuest) { $candidateIntegrityPostRunError = 'Candidate identity or inventory changed during the guest run.' }
    } catch {
        $candidateIntegrityPreservedAfterGuest = $false
        $candidateIntegrityPostRunError = $_.Exception.Message
    }
    $qemuFirmwareCodePreserved = (Get-Sha256 $ovmf) -ceq $qemuCodeSha256
    $uefiVarsTemplatePreserved = (Get-Sha256 $uefiVarsTemplate) -ceq $uefiVarsTemplateSha256
    if (Test-Path -LiteralPath $uefiVarsImage -PathType Leaf) { $uefiVarsImageFinalSha256 = Get-Sha256 $uefiVarsImage }
}
$sourceRamdiskSha256 = Get-Sha256 (Join-Path $repo 'ESP\ramdisk.img')
$sourceRamdiskPreserved = $sourceRamdiskSha256 -ceq 'AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8'
$markerLines = @($serialText -split "`r?`n" | Where-Object { $_ -match '^marker=C188R12R4R5-GC-OWNERSHIP ' })
$markerPhases = @($markerLines | ForEach-Object { if ($_ -match '\bphase=([^ ]+)') { $Matches[1] } })
$startupFailure = $serialText -match '(?m)^\[nativeaot-gc-first-allocation\] RhInitialize return=(?:0x)?00000001\b' -or
    $serialText -match '(?m)^\[nativeaot-gc-first-allocation\] ALL_FAIL\s*$'
$initReturnFailure = $isInitReturnContract -and
    $null -ne $serialAnalysis -and $serialAnalysis.nativeRhInitializeBoolean -eq '00000000' -and
    $serialAnalysis.startupAdapterOutcome -eq 'failure'
$nativeAllocationClassification = if ($null -ne $nativeAllocationAnalysis -and @($nativeAllocationAnalysis.results).Count -eq 1) {
    [string]$nativeAllocationAnalysis.results[0].classification
} else { $null }
$nativeAllocationFirstFailure = if ($null -ne $nativeAllocationAnalysis -and @($nativeAllocationAnalysis.results).Count -eq 1) {
    $nativeAllocationAnalysis.results[0].firstFailure
} else { $null }
$r4GcResult = [regex]::Match('', '(?m)^\[C188R12R4R5-GC-OWNERSHIP\] outcome=(PASS|FAIL) callbacks=(\S+) result1=(\S+) result2=(\S+) result3=(\S+)\s*$')
$outcome = if (-not $sourceRamdiskPreserved) { 'SOURCE_RAMDISK_CHANGED_DURING_GUEST' }
    elseif (-not $preflightPassed) { 'EXPERIMENT_BLOCKED_QAC_CENSUS' }
    elseif ($guestPass -and $releaseVerified -and $qacExitCode -eq 0) { 'GC_GRAPH_GUEST_PROVEN' }
    elseif ($startupFailure) { 'EXPERIMENT_BLOCKED' }
    elseif ($admitted) { 'GC_ROOT_OR_COLLECTION_DEFECT_OR_GUEST_INCOMPLETE' }
    else { 'EXPERIMENT_READY_BUT_GUEST_NOT_ADMITTED' }
if ($isTransitionAttribution) {
    $outcome = if (-not $sourceRamdiskPreserved) { 'SOURCE_RAMDISK_CHANGED_DURING_GUEST' }
        elseif (-not $preflightPassed -or -not $admitted) { 'TRANSITION_DEFERRED_QAC_BUSY' }
        elseif (-not $releaseVerified -or $qacExitCode -ne 0) { 'TRANSITION_NATIVE_GUEST_CLEANUP_UNVERIFIED' }
        elseif (-not $candidateIntegrityPreservedAfterGuest -or -not $qemuFirmwareCodePreserved -or -not $uefiVarsTemplatePreserved) { 'TRANSITION_CANDIDATE_OR_BASELINE_MUTATED' }
        elseif (-not $transitionAttributionCaptureComplete) { 'TRANSITION_ATTRIBUTION_INCOMPLETE' }
        elseif ($transitionAttributionClassification -eq 'INITIALIZEMODULES_RETURNED_MANAGED_ENTRY_NOT_REQUESTED') { 'TRANSITION_NATIVE_GUEST_QUALIFIED' }
        elseif ($transitionAttributionClassification -in @('RUNTIME_FAIL_FAST_ATTRIBUTED','RUNTIME_FAIL_FAST_CALLSITE_UNRESOLVED','TRANSITION_GATE_BLOCKED_SAFE_STOP')) { 'TRANSITION_NATIVE_FAILURE_BOUNDARY_PROVEN' }
        else { 'TRANSITION_ATTRIBUTION_INCOMPLETE' }
} elseif ($isSafeHandoff) {
    $safeAnalyzerResult = if ($null -ne $serialAnalysis) { [string]$serialAnalysis.result } else { '' }
    $outcome = if (-not $sourceRamdiskPreserved) { 'SOURCE_RAMDISK_CHANGED_DURING_GUEST' }
        elseif (-not $preflightPassed) { 'SAFE_HANDOFF_BLOCKED_QAC_CENSUS' }
        elseif (-not $admitted) { 'SAFE_HANDOFF_CANDIDATE_NOT_ADMITTED' }
        elseif (-not $releaseVerified -or $qacExitCode -ne 0) { 'SAFE_HANDOFF_CAPTURED_BUT_QAC_RELEASE_UNVERIFIED' }
        elseif ($safeAnalyzerResult -eq 'MANAGED_TRANSITION_FAILED') { 'MANAGED_TRANSITION_FAILED' }
        elseif ($safeAnalyzerResult -eq 'TOOLING_OR_POSTPROCESSING_FAILURE') { 'TOOLING_OR_POSTPROCESSING_FAILURE' }
        elseif ($safeAnalyzerResult -eq 'DIAGNOSTIC_EVIDENCE_INCOMPLETE' -or -not $initCaptureComplete) { 'DIAGNOSTIC_EVIDENCE_INCOMPLETE' }
        elseif ($guestPass) { 'SAFE_HANDOFF_GUEST_QUALIFIED' }
        else { 'SAFE_HANDOFF_BOUNDARY_NOT_CONFIRMED' }
} elseif ($isInitAttribution) {
    $outcome = if (-not $sourceRamdiskPreserved) { 'SOURCE_RAMDISK_CHANGED_DURING_GUEST' }
        elseif (-not $preflightPassed) { 'INIT_BLOCKED_QAC_CENSUS' }
        elseif (-not $admitted) { 'INIT_CANDIDATE_NOT_ADMITTED' }
        elseif (-not $releaseVerified) { 'INIT_CAPTURED_BUT_QAC_RELEASE_UNVERIFIED' }
        elseif (-not $guestPass) { 'INIT_TRACE_INCOMPLETE' }
        elseif ($serialAnalysis.rhInitializeResult -eq 'failure') { 'INIT_FAILURE_CAPTURED' }
        elseif ($serialAnalysis.rhInitializeResult -eq 'success') { 'INIT_RUNTIME_READY_GC_PROOF_SUPPRESSED' }
        else { 'INIT_CAPTURE_INCOMPLETE' }
} elseif ($isInitReturnContract) {
    $outcome = if (-not $sourceRamdiskPreserved) { 'SOURCE_RAMDISK_CHANGED_DURING_GUEST' }
        elseif (-not $preflightPassed) { 'INIT_BLOCKED_QAC_CENSUS' }
        elseif (-not $admitted) { 'INIT_CANDIDATE_NOT_ADMITTED' }
        elseif (-not $releaseVerified) { 'INIT_CAPTURED_BUT_QAC_RELEASE_UNVERIFIED' }
        elseif (-not $guestPass) { 'INIT_TRACE_OR_RETURN_CONTRACT_INCOMPLETE' }
        elseif ($initReturnFailure) { 'INIT_FAILURE_CAPTURED' }
        elseif ($serialAnalysis.nativeRhInitializeBoolean -eq '00000001') { 'INIT_RETURN_SUCCESS_GC_PROOF_SUPPRESSED' }
        else { 'INIT_CAPTURE_INCOMPLETE' }
} elseif ($isThreadStoreReadiness) {
    $outcome = if (-not $sourceRamdiskPreserved) { 'SOURCE_RAMDISK_CHANGED_DURING_GUEST' }
        elseif (-not $preflightPassed) { 'THREADSTORE_BLOCKED_QAC_CENSUS' }
        elseif (-not $admitted) { 'THREADSTORE_CANDIDATE_NOT_ADMITTED' }
        elseif (-not $releaseVerified) { 'THREADSTORE_CAPTURED_BUT_QAC_RELEASE_UNVERIFIED' }
        elseif (-not $guestPass) { 'THREADSTORE_TRACE_INCOMPLETE' }
        elseif ($serialAnalysis.threadStoreHandoffReady) { 'THREADSTORE_HANDOFF_PROVEN' }
        else { 'THREADSTORE_FAILURE_BOUNDARY_CAPTURED' }
    if ($guestPass -and $serialAnalysis.threadStoreHandoffReady) {
        $outcome = if ($r4GcResult.Groups[1].Value -eq 'PASS') {
            'THREADSTORE_HANDOFF_AND_GC_PROOF_CAPTURED'
        } else { 'THREADSTORE_HANDOFF_CAPTURED_GC_PROOF_FAILED' }
    }
} elseif ($isNativeAllocationFailureAttribution) {
    $outcome = Get-NativeAllocationOutcome -SourceRamdiskPreserved $sourceRamdiskPreserved `
        -PreflightPassed $preflightPassed -Admitted $admitted -ReleaseVerified $releaseVerified `
        -QacExitCode $qacExitCode -SerialCaptured ($null -ne $serialSha256BeforeAnalysis) `
        -RawSerialPreserved $rawSerialPreservedByAnalysis -RecordComplete $nativeAllocationRecordCaptureComplete `
        -AnalysisError $nativeAllocationAnalysisError -Classification $nativeAllocationClassification
} elseif ($isSemiSharedAttribution) {
    $gcInitOutcome = if ($null -ne $gcInternal) { [string]$gcInternal.outcome } else { '' }
    $semiOutcome = if ($null -ne $semiSharedAnalysis) { [string]$semiSharedAnalysis.outcome } else { '' }
    $outcome = if (-not $sourceRamdiskPreserved) { 'SOURCE_RAMDISK_CHANGED_DURING_GUEST' }
        elseif (-not $preflightPassed) { 'SEMISHARED_BLOCKED_QAC_CENSUS' }
        elseif (-not $admitted) { 'SEMISHARED_CANDIDATE_NOT_ADMITTED' }
        elseif (-not $releaseVerified) { 'SEMISHARED_CAPTURED_BUT_QAC_RELEASE_UNVERIFIED' }
        elseif (-not $gcInitRecordCaptureComplete -or -not $semiSharedRecordCaptureComplete) { 'DIAGNOSTIC_INCONCLUSIVE' }
        elseif ($semiOutcome -eq 'failure_branch_attributed') { 'SEMISHARED_FAILURE_BRANCH_CAPTURED' }
        elseif ($semiOutcome -eq 'init_semi_shared_succeeded' -and $gcInitOutcome -eq 'gc_heap_initialize_succeeded') { 'GC_HEAP_INITIALIZATION_ACCEPTED' }
        elseif ($semiOutcome -eq 'init_semi_shared_succeeded') { 'SEMISHARED_SUCCEEDED_LATER_GC_INIT_FAILED' }
        else { 'DIAGNOSTIC_INCONCLUSIVE' }
} elseif ($isGcInitFailureAttribution) {
    $gcInitOutcome = if ($null -ne $gcInternal) { [string]$gcInternal.outcome } else { '' }
    $gcFirstFailureSite = if ($null -ne $gcInternal -and $null -ne $gcInternal.record) {
        [int]$gcInternal.record.firstFailureSite
    } else { -1 }
    $gcHeapResult = if ($null -ne $gcInternal -and $null -ne $gcInternal.record) {
        [string]$gcInternal.record.finalHeapHresult
    } else { $null }
    $outcome = if (-not $sourceRamdiskPreserved) { 'SOURCE_RAMDISK_CHANGED_DURING_GUEST' }
        elseif (-not $preflightPassed) { 'GC_INIT_BLOCKED_QAC_CENSUS' }
        elseif (-not $admitted) { 'GC_INIT_CANDIDATE_NOT_ADMITTED' }
        elseif (-not $releaseVerified) { 'GC_INIT_CAPTURED_BUT_QAC_RELEASE_UNVERIFIED' }
        elseif (-not $gcInitRecordCaptureComplete) { 'DIAGNOSTIC_INCONCLUSIVE' }
        elseif ($gcInitOutcome -in @('init_semi_shared_failed','wait_for_gc_event_allocation_failed','wait_for_gc_event_create_failed')) { 'GC_INIT_ROOT_CAUSE_PROVEN' }
        elseif ($gcInitOutcome -eq 'gc_heap_initialize_succeeded') { 'GC_HEAP_INITIALIZATION_ACCEPTED' }
        elseif ($gcFirstFailureSite -eq 4 -and $gcHeapResult -ne '0x00000000') { 'GC_INIT_FAILURE_LOCALIZED' }
        else { 'DIAGNOSTIC_INCONCLUSIVE' }
}
$transitionRecordManifest = $null
$transitionStateManifest = $null
$transitionCallSiteManifest = $null
$transitionFailFastManifest = $null
if ($null -ne $transitionAttributionAnalysis) {
    $transitionRecordManifest = $transitionAttributionAnalysis.transitionRecord
    $transitionStateManifest = $transitionAttributionAnalysis.threadAndRuntimeState
    $transitionCallSiteManifest = $transitionAttributionAnalysis.callSite
    $transitionFailFastManifest = $transitionAttributionAnalysis.failFast
}
$manifest = [ordered]@{
    result=$outcome;runRoot=$runRoot;candidateRoot=$candidate;candidateManifest=$candidateManifestPath
    candidateManifestSha256=$candidateManifestSha256
    candidateIntegrityInventory=$CandidateIntegrityInventory
    candidateIntegrityInventorySha256=$candidateIntegrityInventorySha256
    candidateIntegrityFileCount=if($isTransitionAttribution -and $null -ne $candidateIntegrityAfterGuest){$candidateIntegrityAfterGuest.FileCount}else{$null}
    candidateIntegrityPreservedAfterGuest=$candidateIntegrityPreservedAfterGuest
    candidateIntegrityPostRunError=$candidateIntegrityPostRunError
    runnerScript=$PSCommandPath;runnerScriptSha256=$runnerScriptSha256
    qemuExecutable=$qemu;qemuExecutableSha256=$qemuSha256
    qualificationCorrelationId=$runCorrelationId
    transitionProfileSelected=$isTransitionAttribution
    candidateEsp=$candidateEsp
    isolatedGuestEsp=if($isTransitionAttribution){$esp}else{$null}
    isolatedEspFileCount=if($isTransitionAttribution){$guestState.EspFileCount}else{$null}
    isolatedRamdiskSha256=if($isTransitionAttribution){$guestState.IsolatedRamdiskSha256}else{$null}
    qemuFirmwareCode=$ovmf;qemuFirmwareCodeSha256=$qemuCodeSha256;qemuFirmwareCodePreserved=$qemuFirmwareCodePreserved
    uefiVarsTemplate=$uefiVarsTemplate;uefiVarsTemplateSha256=$uefiVarsTemplateSha256
    uefiVarsTemplatePreserved=$uefiVarsTemplatePreserved
    writableUefiVarsImage=$uefiVarsImage
    writableUefiVarsImageInitialSha256=if($isTransitionAttribution){$guestState.UefiVarsImageSha256}else{$null}
    writableUefiVarsImageFinalSha256=$uefiVarsImageFinalSha256
    writableUefiVarsImageLengthBytes=if($isTransitionAttribution){$guestState.UefiVarsImageLengthBytes}else{$null}
    uefiVarsTemplateProvenance='C:\Program Files\qemu\share\edk2-i386-vars.fd; copied to a unique run-owned writable pflash image.'
    candidatePeSha256=[string]$candidateManifest.managedPeSha256
    candidateElfSha256=[string]$candidateManifest.managedElfSha256
    candidateKernelSha256=[string]$candidateManifest.specializedKernelSha256
    candidateRamdiskSha256=[string]$candidateManifest.candidateRamdiskSha256
    sourceRamdiskSha256=$sourceRamdiskSha256;sourceRamdiskPreserved=$sourceRamdiskPreserved
    qacPreflightPassed=$preflightPassed;qacAdmissionGranted=$admitted;qacReservationId=$qacReservationId
    qacBeforeWindowsProcessCensus=$beforeWindowsCensus.Path
    qacBeforeInspectionSummary=$beforeInspectionCapture.Path
    qacAfterWindowsProcessCensus=$qacAfterWindowsCensusPath
    qacAfterInspectionRecords=@($qacAfterInspectionRecords.ToArray())
    qacOwnerPid=if ($null -ne $qacProcess) { $qacProcess.Id } else { $null }
    qacExitCode=$qacExitCode;qacReleaseVerified=$releaseVerified;qacGuestBootLimit=1;automaticRetry=$false
    threadStoreAttempt=$ThreadStoreAttempt;attemptGuardPath=$attemptPath
    qemuStatus=$qemuStatus;qemuQueryStatusResponse=$qemuStatusResponse;qmpPort=$qmpPort;qemuArguments=$qemuArgs;guestPass=$guestPass
    qacOwnerUser=if ($null -ne $qacProcess) { [System.Security.Principal.WindowsIdentity]::GetCurrent().Name } else { $null }
    qacReservationInspection=$duringInspectionPath;qacSnapshotRecords=@($qacSnapshotRecords)
    localQemuChildIdentity=$localGuestIdentity
    qacOwnershipIdentityPath=$qacOwnershipIdentityPath
    qacOwnershipIdentity=$qacOwnershipIdentity
    initAttributionProfile=$isInitAttribution;initCaptureComplete=$initCaptureComplete
    transitionAttributionProfile=$isTransitionAttribution
    transitionAttributionAnalysis=$transitionAttributionAnalysisPath
    transitionAttributionAnalysisExitCode=$transitionAttributionAnalysisExitCode
    transitionAttributionAnalysisError=$transitionAttributionAnalysisError
    transitionAttributionAnalysisResult=$transitionAttributionClassification
    transitionAttributionCaptureComplete=$transitionAttributionCaptureComplete
    transitionTerminalSeen=$transitionTerminalSeen
    transitionRecord=$transitionRecordManifest
    transitionThreadAndRuntimeState=$transitionStateManifest
    transitionCallSite=$transitionCallSiteManifest
    transitionFailFast=$transitionFailFastManifest
    transitionNativeBootObserved=($admitted -and $null -ne $qemuStatus -and [bool]$qemuStatus.Valid -and
        ($serialText -match '(?m)^\[C188R12R4R5R1_INIT\] |^\[C188R12R4R5R2R3_'))
    transitionRuntimeStartupObserved=($serialText -match '(?m)^\[C188R12R4R5R1_INIT\] .*stage=(?:adapter|rhInitialize|palInit)|^\[C188R12R4R5R1_INIT\] trace_start|^\[C188R12R4R5R4_THREAD\].*event=(?:adapter_enter|rhinitialize_return)\b')
    transitionThreadStoreObserved=($serialText -match '(?m)^\[C188R12R4R5R4_THREAD\].*event=threadstore\b' -or
        ($null -ne $transitionStateManifest -and [string]$transitionStateManifest.threadStore -notin @('','0','00000000')))
    transitionThreadAttachmentObserved=($serialText -match '(?m)^\[C188R12R4R5R4_THREAD\].*event=transition_attach_(?:complete|incomplete)\b' -or
        ($null -ne $transitionStateManifest -and [string]$transitionStateManifest.attachment -notin @('','0','00000000')))
    transitionInitializeModulesEntryObserved=($serialText -match '(?m)^\[C188R12R4R5R4_THREAD\].*event=module_initialize_enter\b' -or
        ($null -ne $transitionRecordManifest -and [string]$transitionRecordManifest.attempted -notin @('','0','00000000')))
    transitionInitializeModulesCompletionObserved=($serialText -match '(?m)^\[C188R12R4R5R4_THREAD\].*event=module_initialize_complete\b' -or
        ($null -ne $transitionRecordManifest -and [string]$transitionRecordManifest.completed -notin @('','0','00000000')))
    settingsFactoryEntryObserved=($serialText -match '(?m)C188R12R4_FACTORY .*stage=factory-invoke|C188R12R3_LIFETIME .*stage=factory-enter')
    settingsFactoryCompletionObserved=($serialText -match '(?m)C188R12R4_FACTORY .*stage=factory-return|C188R12R3_LIFETIME .*stage=exit-success')
    persistentCompositeLifecycleObserved=($serialText -match '(?m)C188R12R3_LIFETIME .*stage=exit-success')
    transitionLastProgress=([string[]]@($serialText -split "`r?`n" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }) | Select-Object -Last 1)
    transitionOneBootGuardCreated=(Test-Path -LiteralPath $attemptPath -PathType Leaf)
    transitionGuestBootCount=if(Test-Path -LiteralPath $attemptPath -PathType Leaf){1}else{0}
    initReturnContractProfile=$isInitReturnContract
    threadStoreReadinessProfile=$isThreadStoreReadiness
    gcInitFailureAttributionProfile=$isGcInitFailureAttribution
    semiSharedAttributionProfile=$isSemiSharedAttribution
    nativeAllocationFailureAttributionProfile=$isNativeAllocationFailureAttribution
    semiSharedRecordCaptureComplete=$semiSharedRecordCaptureComplete
    semiSharedAttributionOutcome=if(($isSemiSharedAttribution -or $isNativeAllocationFailureAttribution) -and $null -ne $semiSharedAnalysis){$semiSharedAnalysis.outcome}else{$null}
    semiSharedFailureBranch=if(($isSemiSharedAttribution -or $isNativeAllocationFailureAttribution) -and $null -ne $semiSharedAnalysis){$semiSharedAnalysis.summary.firstFailureBranch}else{$null}
    semiSharedCleanupFailure=if(($isSemiSharedAttribution -or $isNativeAllocationFailureAttribution) -and $null -ne $semiSharedAnalysis){$semiSharedAnalysis.summary.cleanupFailureOperation}else{$null}
    semiSharedAnalysis=$semiSharedAnalysisPath
    semiSharedAnalysisExitCode=$semiSharedAnalysisExitCode
    semiSharedAnalysisIssues=if(($isSemiSharedAttribution -or $isNativeAllocationFailureAttribution) -and $null -ne $semiSharedAnalysis){@($semiSharedAnalysis.issues)}else{@()}
    nativeAllocationAnalysis=$nativeAllocationAnalysisPath
    nativeAllocationAnalysisExitCode=$nativeAllocationAnalysisExitCode
    nativeAllocationAnalysisError=$nativeAllocationAnalysisError
    nativeAllocationRecordCaptureComplete=$nativeAllocationRecordCaptureComplete
    nativeAllocationClassification=$nativeAllocationClassification
    nativeAllocationFirstFailure=$nativeAllocationFirstFailure
    nativeAllocationResults=if($null -ne $nativeAllocationAnalysis){@($nativeAllocationAnalysis.results)}else{@()}
    nativeAllocationDiagnosticRecord=$nativeAllocationRecordPath
    nativeAllocationDiagnosticRecordSha256=if($null -ne $nativeAllocationRecordPath -and (Test-Path -LiteralPath $nativeAllocationRecordPath)){Get-Sha256 $nativeAllocationRecordPath}else{$null}
    gcInitDiagnosticRecord=$gcInitRecordPath
    gcInitDiagnosticRecordSha256=if($null -ne $gcInitRecordPath -and (Test-Path -LiteralPath $gcInitRecordPath)){Get-Sha256 $gcInitRecordPath}else{$null}
    semiSharedDiagnosticRecord=$semiSharedRecordPath
    semiSharedDiagnosticRecordSha256=if($null -ne $semiSharedRecordPath -and (Test-Path -LiteralPath $semiSharedRecordPath)){Get-Sha256 $semiSharedRecordPath}else{$null}
    gcInitializationInternalOutcome=if($isGcInitFailureAttribution -and $null -ne $gcInternal){$gcInternal.outcome}else{$null}
    gcInitializationInternalRecord=if($isGcInitFailureAttribution -and $null -ne $gcInternal){$gcInternal.record}else{$null}
    gcInitializationInternalRecordComplete=if($isGcInitFailureAttribution){$gcInitRecordCaptureComplete}else{$false}
    gcInitializationInternalIssues=if($isGcInitFailureAttribution -and $null -ne $gcInternal){@($gcInternal.issues)}else{@()}
    gcInitializationNativeStatusAvailable=if($isGcInitFailureAttribution){[bool]$candidateManifest.gcInitializationInternalRecord.nativeStatusAvailable}else{$false}
    gcInitializationThreadStoreAnalysis=$threadStoreAnalysisPath
    gcInitializationThreadStoreAnalysisExitCode=$threadStoreAnalysisExitCode
    gcInitializationThreadStoreOutcome=if($isGcInitFailureAttribution -and $null -ne $threadStoreAnalysis){$threadStoreAnalysis.outcome}else{$null}
    runnerOwnerIdentity=[ordered]@{processId=$PID;user=[System.Security.Principal.WindowsIdentity]::GetCurrent().Name}
    reservationIdentity=$qacReservationId
    qemuChildIdentity=$localGuestIdentity
    candidateIdentity=[ordered]@{root=$candidate;candidateManifestSha256=Get-Sha256 $candidateManifestPath;
        diagnosticElfSha256=[string]$candidateManifest.managedElfSha256;
        diagnosticKernelSha256=[string]$candidateManifest.specializedKernelSha256;
        ramdiskSha256=[string]$candidateManifest.candidateRamdiskSha256}
    guardIdentity=[ordered]@{path=$attemptPath;sha256=if(Test-Path -LiteralPath $attemptPath){Get-Sha256 $attemptPath}else{$null};attemptedAtUtc=$attemptedUtc;runnerScriptSha256=$runnerScriptSha256;candidateManifestSha256=$candidateManifestSha256;qemuExecutableSha256=$qemuSha256}
    gcInitializationSourceAnalysis=if ($isSafeHandoff) { $gcInitSourceAnalysisPath } else { $null }
    gcInitializationSourceAnalysisExitCode=if ($isSafeHandoff) { $gcInitSourceAnalysisExitCode } else { $null }
    threadStoreReadinessAnalysis=if ($isThreadStoreReadiness -or $isSafeHandoff) { $serialAnalysisPath } elseif ($isGcInitFailureAttribution) { $threadStoreAnalysisPath } else { $null }
    threadStoreReadinessOutcome=if ($isThreadStoreReadiness -and $null -ne $serialAnalysis) { $serialAnalysis.outcome } elseif ($isSafeHandoff -and $null -ne $serialAnalysis) { $serialAnalysis.result } elseif ($isGcInitFailureAttribution -and $null -ne $threadStoreAnalysis) { $threadStoreAnalysis.outcome } else { $null }
    threadStoreHandoffReady=if ($isThreadStoreReadiness -and $null -ne $serialAnalysis) { [bool]$serialAnalysis.threadStoreHandoffReady } elseif ($isGcInitFailureAttribution -and -not $isSafeHandoff -and $null -ne $threadStoreAnalysis) { [bool]$threadStoreAnalysis.threadStoreHandoffReady } else { $false }
    gcOwnershipProofOutcome=if (($isThreadStoreReadiness -or $isGcInitFailureAttribution) -and $r4GcResult.Success) { $r4GcResult.Groups[1].Value } else { $null }
    gcOwnershipProofCallbacks=if (($isThreadStoreReadiness -or $isGcInitFailureAttribution) -and $r4GcResult.Success) { $r4GcResult.Groups[2].Value } else { $null }
    nativeRhInitializeBoolean=if (($isInitReturnContract -or $isGcInitFailureAttribution) -and $null -ne $serialAnalysis) { $serialAnalysis.nativeRhInitializeBoolean } else { $null }
    nativeRhInitializeOutcome=if (($isInitReturnContract -or $isGcInitFailureAttribution) -and $null -ne $serialAnalysis) { $serialAnalysis.nativeRhInitializeOutcome } else { $null }
    startupAdapterStatusCode=if (($isInitReturnContract -or $isGcInitFailureAttribution) -and $null -ne $serialAnalysis) { $serialAnalysis.startupAdapterStatusCode } else { $null }
    startupAdapterOutcome=if (($isInitReturnContract -or $isGcInitFailureAttribution) -and $null -ne $serialAnalysis) { $serialAnalysis.startupAdapterOutcome } else { $null }
    moduleRegistrationAttemptedByCandidate=if ($isSafeHandoff -and $null -ne $serialAnalysis) { [bool]$serialAnalysis.moduleRegistrationAccepted } else { Test-ModuleRegistrationStageObserved $serialAnalysis }
    moduleRegistrationStageObserved=if ($isSafeHandoff -and $null -ne $serialAnalysis) { [bool]$serialAnalysis.moduleRegistrationAccepted } else { Test-ModuleRegistrationStageObserved $serialAnalysis }
    managedMainObserved=if ($isSafeHandoff) {
        Get-AnalysisBoolean $serialAnalysis 'managedEntryReached'
    } elseif ($isGcInitFailureAttribution) {
        Get-AnalysisBoolean $threadStoreAnalysis 'managedEntryReached'
    } elseif ($isThreadStoreReadiness) {
        Get-AnalysisBoolean $serialAnalysis 'managedEntryReached'
    } else { Get-AnalysisBoolean $serialAnalysis 'managedMainDispatched' }
    managedMainGateReason=if ($isInitReturnContract -or $isNativeAllocationFailureAttribution) { [string]$candidateManifest.managedMainGateReason } else { $null }
    serialAnalysis=$serialAnalysisPath;serialAnalysisExitCode=$serialAnalysisExitCode
    serialAnalysisError=$serialAnalysisError;serialAnalysisComplete=if ($isSafeHandoff -and $null -ne $serialAnalysis) { [bool]$serialAnalysis.diagnosticEvidenceComplete } elseif ($null -ne $serialAnalysis) { [bool]$serialAnalysis.initTraceComplete } else { $false }
    diagnosticEvidenceComplete=if ($isSafeHandoff -and $null -ne $serialAnalysis) { [bool]$serialAnalysis.diagnosticEvidenceComplete } else { $false }
    diagnosticErrorStatus=if ($isSafeHandoff -and $null -ne $serialAnalysis) { $serialAnalysis.diagnosticErrorStatus } else { $null }
    runtimeErrorStatus=if ($isSafeHandoff -and $null -ne $serialAnalysis) { $serialAnalysis.runtimeErrorStatus } else { $null }
    managedTransitionIntentionallyNotAttempted=if ($isSafeHandoff -and $null -ne $serialAnalysis) { [bool]$serialAnalysis.managedTransitionIntentionallyNotAttempted } else { $false }
    managedTransitionAttempted=if ($isSafeHandoff -and $null -ne $serialAnalysis) { [bool]$serialAnalysis.managedTransitionAttempted } else { $false }
    managedTransitionFailed=if ($isSafeHandoff -and $null -ne $serialAnalysis) { [bool]$serialAnalysis.managedTransitionFailed } else { $false }
    managedRuntimeAcceptance=$false
    safeStopTerminalObserved=$safeHandoffTerminal
    threadStorePrerequisites=if ($isSafeHandoff -and $null -ne $serialAnalysis) { $serialAnalysis.threadStorePrerequisites } else { $null }
    serialSha256BeforeAnalysis=$serialSha256BeforeAnalysis;serialSha256AfterAnalysis=$serialSha256AfterAnalysis
    rawSerialPreservedByAnalysis=$rawSerialPreservedByAnalysis
    managedMarkerCount=$markerLines.Count;managedMarkerPhases=$markerPhases;startupFailure=$startupFailure
    serialLog=$serial;serialLogSha256=if (Test-Path -LiteralPath $serial) { Get-Sha256 $serial } else { $null }
    qmpLog=$qmpLog;qmpLogSha256=if (Test-Path -LiteralPath $qmpLog) { Get-Sha256 $qmpLog } else { $null }
    qacBeforeStatus=$qacBeforeStatusPath;qacBeforeList=$qacBeforeListPath
    qacDuringStatus=$qacDuringStatusPath;qacDuringList=$qacDuringListPath
    qacAfterStatus=$qacAfterStatusPath;qacAfterList=$qacAfterListPath
    qacArguments=(Join-Path $runRoot 'qac-run-arguments.json');guestAttempt=$attemptPath;error=$runError
}
$manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $manifestPath -Encoding utf8
if ($isTransitionAttribution) {
    Write-Output "C188R12R4R5R2R3_TRANSITION=$outcome ADMITTED=$admitted CAPTURE_COMPLETE=$transitionAttributionCaptureComplete CLASSIFICATION=$transitionAttributionClassification QAC_RELEASE=$releaseVerified"
    Write-Output "C188R12R4R5R2R3_TRANSITION_MANIFEST=$manifestPath"
} elseif ($isSafeHandoff) {
    Write-Output "C188R12R4R5R4R5R2R2R4R2R1_SAFE_HANDOFF=$outcome ADMITTED=$admitted EVIDENCE_COMPLETE=$initCaptureComplete QAC_RELEASE=$releaseVerified"
    Write-Output "C188R12R4R5R4R5R2R2R4R2R1_SAFE_HANDOFF_MANIFEST=$manifestPath"
} elseif ($isNativeAllocationFailureAttribution) {
    $nativeAttemptLabel = "C188R12R4R5R4R5$NativeAllocationGuestAttempt"
    Write-Output "${nativeAttemptLabel}_NATIVE_ALLOC_ATTRIBUTION=$outcome ADMITTED=$admitted RECORD_COMPLETE=$nativeAllocationRecordCaptureComplete QAC_RELEASE=$releaseVerified"
    Write-Output "${nativeAttemptLabel}_NATIVE_ALLOC_ATTRIBUTION_MANIFEST=$manifestPath"
} elseif ($isSemiSharedAttribution) {
    Write-Output "C188R12R4R5R4R5R2R1_SEMISHARED_ATTRIBUTION=$outcome ADMITTED=$admitted RECORD_COMPLETE=$initCaptureComplete QAC_RELEASE=$releaseVerified"
    Write-Output "C188R12R4R5R4R5R2R1_SEMISHARED_ATTRIBUTION_MANIFEST=$manifestPath"
} elseif ($isGcInitFailureAttribution) {
    Write-Output "C188R12R4R5R4R5R1_GC_INIT_ATTRIBUTION=$outcome ADMITTED=$admitted RECORD_COMPLETE=$initCaptureComplete QAC_RELEASE=$releaseVerified"
    Write-Output "C188R12R4R5R4R5R1_GC_INIT_ATTRIBUTION_MANIFEST=$manifestPath"
} elseif ($isInitAttribution) {
    Write-Output "C188R12R4R5R1_INIT_ATTRIBUTION=$outcome ADMITTED=$admitted CAPTURE_COMPLETE=$initCaptureComplete QAC_RELEASE=$releaseVerified"
    Write-Output "C188R12R4R5R1_INIT_ATTRIBUTION_MANIFEST=$manifestPath"
} elseif ($isInitReturnContract) {
    Write-Output "C188R12R4R5R3_INIT_RETURN_CONTRACT=$outcome ADMITTED=$admitted CAPTURE_COMPLETE=$initCaptureComplete QAC_RELEASE=$releaseVerified"
    Write-Output "C188R12R4R5R3_INIT_RETURN_CONTRACT_MANIFEST=$manifestPath"
} elseif ($isThreadStoreReadiness) {
    $phaseLabel = if ($ThreadStoreAttempt -eq 'R2') { 'C188R12R4R5R4R2' }
        elseif ($ThreadStoreAttempt -eq 'R3') { 'C188R12R4R5R4R3' }
        else { 'C188R12R4R5R4' }
    Write-Output "${phaseLabel}_THREADSTORE_READINESS=$outcome ADMITTED=$admitted CAPTURE_COMPLETE=$initCaptureComplete QAC_RELEASE=$releaseVerified"
    Write-Output "${phaseLabel}_THREADSTORE_READINESS_MANIFEST=$manifestPath"
} else {
    Write-Output "C188R12R4R5_GC_OWNERSHIP=$outcome ADMITTED=$admitted PASS=$guestPass QAC_RELEASE=$releaseVerified"
    Write-Output "C188R12R4R5_GC_OWNERSHIP_MANIFEST=$manifestPath"
}
if (-not $releaseVerified -and $admitted) { exit 4 }
if (-not $sourceRamdiskPreserved) { exit 6 }
if (-not $preflightPassed) { exit 3 }
if ($isInitDiagnostic) {
    if (-not $guestPass) { exit 2 }
    if (-not $rawSerialPreservedByAnalysis) { exit 7 }
    if ($qacExitCode -ne 0) { exit 5 }
    exit 0
}
if (-not $guestPass) { exit 2 }
if (-not $releaseVerified -or $qacExitCode -ne 0) { exit 5 }
