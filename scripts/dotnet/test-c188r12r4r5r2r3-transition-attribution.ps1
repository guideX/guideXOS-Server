param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$OutputDirectory = '',
    [Parameter(Mandatory=$true)][string]$ProductionNativeAotManifest,
    [Parameter(Mandatory=$true)][string]$ProductionNativeAotMap,
    [Parameter(Mandatory=$true)][string]$ProductionApplicationManifest,
    [Parameter(Mandatory=$true)][string]$CallSiteMapPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [System.IO.Path]::GetFullPath($RepoRoot)
$runId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$testRoot = if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    Join-Path $root "out\dotnet\c188r12r4r5r4r5r2r2r4r2r3-transition-attribution\hosted-$runId"
} else { [System.IO.Path]::GetFullPath($OutputDirectory) }
if (Test-Path -LiteralPath $testRoot) { throw "Refusing to overwrite hosted evidence: $testRoot" }
New-Item -ItemType Directory -Force -Path $testRoot | Out-Null

$fixtureResults = [System.Collections.Generic.List[object]]::new()
function Assert-Fixture([bool]$Condition,[string]$Name,[string]$Kind='source-backed') {
    if (-not $Condition) { throw "Transition-attribution fixture failed: $Name" }
    $fixtureResults.Add([ordered]@{ name=$Name; result='PASS'; evidenceKind=$Kind })
}
function Hash-File([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

$headerPath = Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_transition_attribution.h'
$platformPath = Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_platform.cpp'
$palPath = Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_pal_minwin_replacement.cpp'
$kernelPalPath = Join-Path $root 'kernel\core\nativeaot_pal_qemu_test.cpp'
$candidateBuilderPath = Join-Path $root 'scripts\dotnet\build-c188r12r4r5r1-init-attribution-candidate.ps1'
$runtimeBuilderPath = Join-Path $root 'tools\dotnet\runtime-pack\build-runtime-pack.ps1'
$analyzerPath = Join-Path $root 'scripts\dotnet\analyze-c188r12r4r5r2r2r4-safe-handoff.ps1'
$transitionAnalyzerPath = Join-Path $root 'scripts\dotnet\analyze-c188r12r4r5r2r3-transition-attribution.ps1'
$safeTestsPath = Join-Path $root 'scripts\dotnet\test-c188r12r4r5r2r2r4-safe-handoff.ps1'
$harnessPath = Join-Path $root 'tests\dotnet\runtime-pack\c188r12r4r5r2r3_transition_attribution_test.cpp'
foreach ($path in @($headerPath,$platformPath,$palPath,$kernelPalPath,$candidateBuilderPath,$runtimeBuilderPath,
        $analyzerPath,$transitionAnalyzerPath,$safeTestsPath,$harnessPath,$CallSiteMapPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required transition fixture input missing: $path" }
}

$header = Get-Content -LiteralPath $headerPath -Raw
$platform = Get-Content -LiteralPath $platformPath -Raw
$pal = Get-Content -LiteralPath $palPath -Raw
$kernelPal = Get-Content -LiteralPath $kernelPalPath -Raw
$candidateBuilder = Get-Content -LiteralPath $candidateBuilderPath -Raw
$runtimeBuilder = Get-Content -LiteralPath $runtimeBuilderPath -Raw
$safeTestsSource = Get-Content -LiteralPath $safeTestsPath -Raw

$compiler = (Get-Command g++ -ErrorAction Stop).Source
$testExe = Join-Path $testRoot 'transition-attribution-test.exe'
$compileLog = Join-Path $testRoot 'transition-attribution-compile.log'
$testLog = Join-Path $testRoot 'transition-attribution-test.log'
$compileOutput = & $compiler -std=c++17 -Wall -Wextra -Werror -I $root $harnessPath -o $testExe 2>&1
$compileExit = $LASTEXITCODE
[System.IO.File]::WriteAllLines($compileLog,@($compileOutput | ForEach-Object { [string]$_ }),[System.Text.UTF8Encoding]::new($false))
if ($compileExit -ne 0) { throw "Transition gate fixture compile failed ($compileExit): $compileLog" }
$testOutput = & $testExe 2>&1
$testExit = $LASTEXITCODE
[System.IO.File]::WriteAllLines($testLog,@($testOutput | ForEach-Object { [string]$_ }),[System.Text.UTF8Encoding]::new($false))
if ($testExit -ne 0 -or ($testOutput -join "`n") -notmatch 'result=PASS synthetic=1') {
    throw "Synthetic transition gate fixtures failed ($testExit): $testLog"
}

$safeTestOutput = Join-Path $testRoot 'safe-handoff-fixtures'
& pwsh -NoProfile -ExecutionPolicy Bypass -File $safeTestsPath -RepoRoot $root -OutputDirectory $safeTestOutput |
    Set-Content -LiteralPath (Join-Path $testRoot 'safe-handoff-fixtures.log') -Encoding utf8
if ($LASTEXITCODE -ne 0) { throw 'Safe-handoff regression fixtures failed during transition attribution tests.' }
$safeTestResultsPath = Join-Path $safeTestOutput 'hosted-test-results.json'
$safeTestResults = Get-Content -LiteralPath $safeTestResultsPath -Raw | ConvertFrom-Json
if ($safeTestResults.result -cne 'PASS' -or [bool]$safeTestResults.qemuLaunched -or
    [bool]$safeTestResults.qacReserved -or
    -not (@($safeTestResults.analyzerCases) -contains 'malformed or contradictory milestone ordering')) {
    throw 'Safe-handoff source/analyzer fixtures did not pass or crossed the hosted-only boundary.'
}

$syntheticMapPath = Join-Path $testRoot 'synthetic-callsite-map.json'
$syntheticMap = [ordered]@{
    schemaVersion=1
    lockedRuntimeCommit='synthetic-fixture-only'
    sites=@(
        [ordered]@{
            id=1; key='THREAD_REVERSE_PINVOKE'; name='synthetic.reverse-pinvoke'
            message='Synthetic message ID'; linkedInCandidate=$true
            callerReturnAddress='0x0000000000001001'
        },
        [ordered]@{
            id=2; key='CPU_FEATURES'; name='synthetic.unlinked-source-caller'
            message='Synthetic unlinked message ID'; linkedInCandidate=$false
            resolutionStatus='source-caller-not-linked-to-candidate'
            callerReturnAddress='0x0000000000000000'
        }
    )
}
[System.IO.File]::WriteAllText($syntheticMapPath,($syntheticMap | ConvertTo-Json -Depth 6),[System.Text.UTF8Encoding]::new($false))
function Write-TransitionFixture([string]$Name,[string[]]$Lines,[string]$MapPath=$syntheticMapPath) {
    $serialPath = Join-Path $testRoot "$Name.serial.log"
    $reportPath = Join-Path $testRoot "$Name.analysis.json"
    [System.IO.File]::WriteAllText($serialPath,(($Lines -join "`n") + "`n"),[System.Text.UTF8Encoding]::new($false))
    & pwsh -NoProfile -ExecutionPolicy Bypass -File $transitionAnalyzerPath -SerialPath $serialPath `
        -CallSiteMapPath $MapPath -OutputPath $reportPath | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Transition analyzer failed its synthetic fixture $Name." }
    return (Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json)
}
$fatalRecord = '[C188R12R4R5R2R3_FATAL] valid=1 seq=0000000000000001 snapshot=00000001 attempted=00000001 completed=00000000 gate=00000000 fatal=00000001 reason=50414C06 callSite=00000001 callerRip=0000000000001001 msgState=00000000 msgBytes=00000000 msgTruncated=00000000 originalStatusAvailable=00000000 originalStatus=00000000 firstFailureStage=FFFFFFFF firstFailureStatusAvailable=00000000 firstFailureStatus=00000000 lastSuccess=00000062 initOverflow=00000000 threadOverflow=00000000'
$stateRecord = '[C188R12R4R5R2R3_STATE] runtime=00000001 rhInitialize=00000001 threadStore=00000001 rawThread=0000000000001000 currentThread=0000000000001000 attachment=00000002 nativeThreadId=0000000000000042 observedOsThreadId=0000000000000042 coop=00000001 tlsReady=00000001 tlsIndex=0000000000000000 palFlsIndex=0000000000000001 palFlsValue=0000000000001000 flsState=00000001 stackReady=00000001 stackCurrent=0000000000003000 stackLow=0000000000002000 stackHigh=0000000000004000 frame=FFFFFFFFFFFFFFFF frameReady=00000001 frameAligned=00000001 frameInStack=00000000 stockWillAttach=00000000 gcReady=00000001 module=00000001 codeManager=00000001 modulesNotInitialized=00000001'
$attributedFailFast = Write-TransitionFixture 'synthetic-attributed-failfast' @(
    $fatalRecord,$stateRecord,'[nativeaot-pal-qemu-test] FAIL_FAST reason=50414C06 detail=0000000000000000')
if ($attributedFailFast.classification -ne 'RUNTIME_FAIL_FAST_ATTRIBUTED' -or
    $attributedFailFast.callSite.id -ne 1 -or $attributedFailFast.failFast.underlyingDefectInferred) {
    throw 'Synthetic fatal call-site attribution or uninterpreted reason classification failed.'
}
$unknownCallsite = Write-TransitionFixture 'synthetic-unresolved-callsite' @(
    ($fatalRecord.Replace('callSite=00000001 callerRip=0000000000001001','callSite=FFFFFFFE callerRip=0000000000009999')),
    $stateRecord,'[nativeaot-pal-qemu-test] FAIL_FAST reason=50414C06 detail=0000000000000000')
if ($unknownCallsite.classification -ne 'RUNTIME_FAIL_FAST_CALLSITE_UNRESOLVED') {
    throw 'Unknown synthetic caller RIP was incorrectly attributed.'
}
$blockedRecord = $fatalRecord.Replace('snapshot=00000001 attempted=00000001 completed=00000000 gate=00000000 fatal=00000001 reason=50414C06 callSite=00000001 callerRip=0000000000001001',
    'snapshot=00000002 attempted=00000000 completed=00000000 gate=00000004 fatal=00000000 reason=00000000 callSite=FFFFFFFE callerRip=0000000000000000')
$palFlsMismatchState = $stateRecord.Replace('palFlsIndex=0000000000000001 palFlsValue=0000000000001000 flsState=00000001','palFlsIndex=0000000000000001 palFlsValue=0000000000000000 flsState=00000000')
$blockedRecord = $blockedRecord.Replace('gate=00000004','gate=00000006')
$blocked = Write-TransitionFixture 'synthetic-gate-blocked' @(
    $blockedRecord,$palFlsMismatchState,
    '[C188R12R4R5R2R3_TRANSITION] result=safe_stop status=00000106 managedEntry=not_reached collection=not_requested runtimeError=none')
if ($blocked.classification -ne 'TRANSITION_GATE_BLOCKED_SAFE_STOP' -or
    $blocked.transitionRecord.gate -ne '00000006' -or
    $blocked.threadAndRuntimeState.flsState -ne '00000000' -or
    $blocked.threadAndRuntimeState.attachment -ne '00000002') {
    throw 'Synthetic transition preflight failure was not classified as a safe stop.'
}
$returnedRecord = ($fatalRecord.Replace('attempted=00000001 completed=00000000',
    'attempted=00000001 completed=00000001')).Replace('fatal=00000001','fatal=00000000')
$returned = Write-TransitionFixture 'synthetic-initialize-modules-returned' @(
    $returnedRecord,$stateRecord,
    '[C188R12R4R5R2R3_TRANSITION] result=initialize_modules_returned managedEntry=not_requested collection=not_requested')
if ($returned.classification -ne 'INITIALIZEMODULES_RETURNED_MANAGED_ENTRY_NOT_REQUESTED') {
    throw 'InitializeModules return was incorrectly interpreted as managed-entry or GC acceptance.'
}
$mismatch = Write-TransitionFixture 'synthetic-reason-mismatch' @(
    $fatalRecord,$stateRecord,'[nativeaot-pal-qemu-test] FAIL_FAST reason=11111111 detail=0000000000000000')
if ($mismatch.classification -ne 'TRANSITION_ATTRIBUTION_INCOMPLETE' -or
    $mismatch.meaning -ne 'runtime_failfast_and_fatal_record_disagree') {
    throw 'Contradictory fail-fast evidence was not rejected.'
}
$unlinkedSite = Write-TransitionFixture 'synthetic-unlinked-source-callsite' @(
    ($fatalRecord.Replace('callSite=00000001 callerRip=0000000000001001','callSite=00000002 callerRip=0000000000000000')),
    $stateRecord,'[nativeaot-pal-qemu-test] FAIL_FAST reason=50414C06 detail=0000000000000000')
if ($unlinkedSite.classification -ne 'TRANSITION_ATTRIBUTION_INCOMPLETE' -or
    $unlinkedSite.parseError -ne 'unknown_callsite_id') {
    throw 'A source caller excluded from the linked candidate was incorrectly accepted as an attributed call site.'
}

$safeSerial = Join-Path $root 'out\dotnet\c188r12r4r5r4r5r2r2r4r2-safe-handoff\qac-guest-r2r2r4r2r1-001\serial.log'
$safeReviewedPath = Join-Path $root 'out\dotnet\c188r12r4r5r4r5r2r2r4r2r2-diagnostic-recovery\recovery-analysis\safe-handoff-reviewed-r2r1-final.json'
$r4Serial = Join-Path $root 'out\dotnet\c188r12r4r5r4r5r2r2r4-pal-capacity-fix\qac-guest-r2r2r4-001\serial.log'
$r4ReviewedPath = Join-Path $root 'out\dotnet\c188r12r4r5r4r5r2r2r4r2r2-diagnostic-recovery\recovery-analysis\prior-transition-failfast-reviewed-r4.json'
foreach ($path in @($safeSerial,$safeReviewedPath,$r4Serial,$r4ReviewedPath,
        $ProductionNativeAotManifest,$ProductionNativeAotMap,$ProductionApplicationManifest)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Preserved or production evidence missing: $path" }
}

$safeSerialHash = Hash-File $safeSerial
$r4SerialHash = Hash-File $r4Serial
$safeReviewed = Get-Content -LiteralPath $safeReviewedPath -Raw | ConvertFrom-Json
$r4Reviewed = Get-Content -LiteralPath $r4ReviewedPath -Raw | ConvertFrom-Json
$prodRuntime = Get-Content -LiteralPath $ProductionNativeAotManifest -Raw | ConvertFrom-Json
$prodApplication = Get-Content -LiteralPath $ProductionApplicationManifest -Raw | ConvertFrom-Json
$prodMap = Get-Content -LiteralPath $ProductionNativeAotMap -Raw

# 1. Prior successful initialization and registration followed by safe stop remain accepted.
Assert-Fixture ($safeReviewed.result -eq 'SAFE_HANDOFF_INTENTIONALLY_STOPPED' -and
    $safeReviewed.initializationAccepted -and $safeReviewed.moduleRegistrationAccepted -and
    $safeReviewed.managedTransitionIntentionallyNotAttempted -and
    -not $safeReviewed.managedEntryReached) 'successful initialization followed by intentional safe-stop'

# 2-3. This mode is opt-in, and both build interfaces default to switch-off.
Assert-Fixture ($candidateBuilder -match '\[switch\]\$InitializeModulesAttribution' -and
    $candidateBuilder.Contains("if (`$InitializeModulesAttribution) { `$managedBuildArguments += '-C188R12R4R5R2R3InitializeModulesAttribution' }") -and
    $runtimeBuilder -match '\[switch\]\$C188R12R4R5R2R3InitializeModulesAttribution') 'explicit diagnostic mode selection'
Assert-Fixture ($candidateBuilder -match '\[switch\]\$InitializeModulesAttribution' -and
    $candidateBuilder.Contains('if ($InitializeModulesAttribution)') -and
    $candidateBuilder.Contains('InitializeModules attribution requires the complete explicitly selected')) 'transition mode disabled by default'

# 4-8. Shared pure evaluator, exercised in the compiled hosted fixture.
Assert-Fixture (($testOutput -join "`n") -match 'missing ThreadStore publication blocks transition') 'missing ThreadStore publication blocks' 'synthetic'
Assert-Fixture (($testOutput -join "`n") -match 'current ThreadStore thread with empty PAL FLS blocks at gate 6' -and
    ($testOutput -join "`n") -match 'current ThreadStore thread with foreign PAL FLS blocks at gate 6') 'attached current thread rejects absent or foreign PAL FLS' 'synthetic gate fixture'
Assert-Fixture (($testOutput -join "`n") -match 'synthetic attached preemptive thread with top-of-stack marker passes') 'valid attachment-state observation' 'synthetic'
Assert-Fixture (($testOutput -join "`n") -match 'attached PAL thread identity must match current ThreadStore OS identity') 'PAL and ThreadStore identities must match' 'synthetic gate fixture'
Assert-Fixture (($testOutput -join "`n") -match 'missing transition frame blocks attached thread') 'missing or malformed transition frame blocks' 'synthetic'
Assert-Fixture (($testOutput -join "`n") -match 'synthetic candidate RIP resolves to stable source call-site ID') 'fatal-error call-site attribution' 'synthetic map fixture; candidate map is generated at build'
Assert-Fixture (($testOutput -join "`n") -match 'fixed-size fatal record seals with stable call-site reason') 'bounded message or reason capture' 'synthetic record; runtime uses stable IDs'
Assert-Fixture (($testOutput -join "`n") -match 'synthetic truncated-message record is representable') 'truncated fatal-message record' 'synthetic'

# 10-13. The actual capture leaves text unavailable, keeps fail-fast, and preserves first failure/overflow.
$palCallback = [regex]::Match($pal,'(?s)PalPrintFatalError\(const char\* message\)\s*\{(?<body>.*?)\n\}')
$fatalCallback = [regex]::Match($platform,'(?s)GuideXosNativeAotTransitionCapturePalFatal\(.*?\n\}')
Assert-Fixture ($palCallback.Success -and $palCallback.Groups['body'].Value -match 'GuideXosNativeAotTransitionCapturePalFatal\(\s*message,\s*reinterpret_cast<uintptr_t>\(_ReturnAddress\(\)\)\)' -and
    $fatalCallback.Success -and $fatalCallback.Value -match 'messagePointer\s*=\s*reinterpret_cast<uintptr_t>\(message\)' -and
    $fatalCallback.Value -notmatch 'message\s*\[|strlen\s*\(|strnlen\s*\(') 'fatal message unavailable without unsafe dereference'
Assert-Fixture ($pal -match 'GuideXosNativeAotTransitionCapturePalFatal\(\s*message,\s*reinterpret_cast<uintptr_t>\(_ReturnAddress\(\)\)\);\s*#endif\s*\(void\)message;\s*guidexos_nativeaot_pal_fail_fast\(0x50414C06u\);') 'original fail-fast semantics preserved'
Assert-Fixture ($platform -match 'firstFailureStatus\s*=\s*record\.firstFailureStatusAvailable\s*!=\s*0u\s*\?\s*GuideXosNativeAotInitTraceGetFirstFailureStatus\(\)\s*:\s*0u' -and
    $header.Contains('firstFailureStatus') -and $header.Contains('fatalReason')) 'first-failure status retained independently'
Assert-Fixture ($platform.Contains('record.initTraceOverflow = GuideXosNativeAotInitTraceGetOverflow()') -and
    $header.Contains('initTraceOverflow') -and $header.Contains('GX_TRANSITION_GATE_')) 'diagnostic overflow remains classified and explicit'

# 14-15. Shared ordering gate and contradictory trace analyzer regression.
Assert-Fixture (($testOutput -join "`n") -match 'synthetic stock attachment path passes with zeroed TLS Thread' -and
    $header.Contains('GuideXosTransitionEvaluateGate')) 'successful transition prerequisite ordering' 'synthetic gate fixture'
Assert-Fixture ($platform -match 'void\* palFlsValue = GuideXosNativeAotTransitionGetPalFlsValue\(\)' -and
    $platform.Contains('record.flsCurrentThread = reinterpret_cast<uintptr_t>(palFlsValue)') -and
    $pal -match 'return g_flsIndex == kFlsOutOfIndexes\s*\?\s*nullptr\s*:\s*guidexos_nativeaot_pal_fls_get\(g_flsIndex\)') 'gate observes the allocated PAL FLS slot through the active PAL getter'

# PAL slot and ThreadStore publication contract: all accesses must use the same slot and current execution context.
$palInitBody = [regex]::Match($pal,'(?s)PalInit\(\)\s*\{(?<body>.*?)\n\}')
$palAttachBody = [regex]::Match($pal,'(?s)PalAttachThread\(void\* thread\)\s*\{(?<body>.*?)\n\}')
$palDetachBody = [regex]::Match($pal,'(?s)PalDetachThread\(void\* thread\)\s*\{(?<body>.*?)\n\}')
Assert-Fixture ($palInitBody.Success -and
    $palInitBody.Groups['body'].Value.IndexOf('guidexos_nativeaot_pal_fls_free(g_flsIndex)',[StringComparison]::Ordinal) -lt
        $palInitBody.Groups['body'].Value.IndexOf('guidexos_nativeaot_pal_fls_alloc(FiberDetachCallback)',[StringComparison]::Ordinal) -and
    $palInitBody.Groups['body'].Value.Contains('GX_THREADSTORE_EVENT_PAL_FLS_ALLOCATE')) 'repeated PAL initialization releases the old slot before allocating the current slot'
Assert-Fixture ($palAttachBody.Success -and
    $palAttachBody.Groups['body'].Value.IndexOf('guidexos_nativeaot_pal_fls_get(g_flsIndex)',[StringComparison]::Ordinal) -lt
        $palAttachBody.Groups['body'].Value.IndexOf('guidexos_nativeaot_pal_fls_set(g_flsIndex, thread)',[StringComparison]::Ordinal) -and
    $palAttachBody.Groups['body'].Value.Contains('GX_THREADSTORE_EVENT_PAL_ATTACH_COMPLETE')) 'PAL attach reads then writes the same allocated slot before reporting completion'
Assert-Fixture ($palAttachBody.Success -and
    $palAttachBody.Groups['body'].Value.Contains('guidexos_nativeaot_pal_fail_fast(0x50414C02u)') -and
    $palAttachBody.Groups['body'].Value.Contains('guidexos_nativeaot_pal_fail_fast(0x50414C03u)') -and
    $palDetachBody.Success -and
    $palDetachBody.Groups['body'].Value.Contains('guidexos_nativeaot_pal_fail_fast(0x50414C04u)')) 'foreign existing attachment, failed assignment, and stale detach are rejected'
Assert-Fixture ($palDetachBody.Success -and
    $palDetachBody.Groups['body'].Value.Contains('guidexos_nativeaot_pal_fls_set(g_flsIndex, nullptr)') -and
    $palDetachBody.Groups['body'].Value.Contains('GX_THREADSTORE_EVENT_PAL_DETACH_COMPLETE') -and
    $kernelPal.Contains('threadstore::detachCurrentThread()')) 'PAL teardown clears its slot and worker teardown calls ThreadStore detach'
Assert-Fixture ($kernelPal.Contains('const uint64_t id = process::current_thread_id();') -and
    $kernelPal -match '(?s)currentThreadState\(\).*?process::current_thread_id\(\).*?state\.threadId == id' -and
    $kernelPal.Contains('table->fls_get = bridgeFlsGet') -and
    $kernelPal.Contains('table->fls_set = bridgeFlsSet') -and
    $kernelPal.Contains('getLocalStorageValue(g_flsSlots[index].genericIndex, &marker)') -and
    $kernelPal.Contains('markerFromValue(marker)') -and
    $kernelPal -match '(?s)setLocalStorageValue\(g_flsSlots\[index\]\.genericIndex,\s*value == nullptr \? nullptr : &cell\)' -and
    $kernelPal.Contains('palFlsReadinessMarker("pal_fls_get_context"') -and
    $kernelPal.Contains('palFlsReadinessMarker("pal_fls_get_storage"') -and
    $kernelPal.Contains('palFlsReadinessMarker("pal_fls_get_owner"') -and
    $kernelPal.Contains('palFlsReadinessMarker("pal_fls_set_context"') -and
    $kernelPal.Contains('palFlsReadinessMarker("pal_fls_set_cell"') -and
    $kernelPal.Contains('palFlsReadinessMarker("pal_fls_set_storage"')) 'diagnostic PAL FLS resolves the actual QEMU bridge context and records get/set identity'
Assert-Fixture (@($safeTestResults.analyzerCases) -contains 'malformed or contradictory milestone ordering' -and
    [int]$safeTestResults.analyzerCaseCount -ge 22) 'malformed or contradictory stage sequence rejected' 'synthetic analyzer fixture'

# 16-17. Verify the old failing guest and recovered safe-stop are intact and reviewed as documented.
Assert-Fixture ($r4Reviewed.result -eq 'MANAGED_TRANSITION_FAILED' -and
    $r4Reviewed.failFast.code -eq '0x50414C06' -and
    $r4SerialHash -eq 'D9F14AC9DB9E39868D26F2CDBA798815D6A33C89A475DC9BEAFE998FFAD28FBF') 'previous R4 fail-fast evidence preserved'
Assert-Fixture ($safeReviewed.trace.stageCount -eq 29 -and $safeReviewed.trace.eventCount -eq 93 -and
    $safeReviewed.trace.overflow -eq 0 -and $safeReviewed.trace.serialDropped -eq 0 -and
    $safeSerialHash -eq '27C653B08FCF0CBBD4696498FE86AD97591DA3EFFB7F27BD40A2230A18CE425C') 'recovered 29-stage 93-event safe-stop evidence preserved'

# 18. Proof-disabled production must exclude diagnostic controls and preserve lifecycle behavior.
$transitionMarker = 'C188R12R4R5R2R3_INITIALIZEMODULES_ATTRIBUTION'
$transitionExport = 'GuideXosNativeAotC188R12R4R5R2R3InitializeModulesAttributionPrepare'
Assert-Fixture (-not [bool]$prodRuntime.c188r12r4r5r2r3InitializeModulesAttribution -and
    -not $prodMap.Contains($transitionMarker) -and -not $prodMap.Contains($transitionExport) -and
    [bool]$prodApplication.persistentCompositeLifecycle) 'production instrumentation exclusion and persistent lifecycle'
Assert-Fixture ($attributedFailFast.callSite.message -eq 'Synthetic message ID' -and
    $attributedFailFast.transitionRecord.callerRip -eq '0000000000001001' -and
    $attributedFailFast.evidencePreserved) 'transition analyzer resolves exact 16-digit synthetic RIP without mutating serial' 'synthetic'
Assert-Fixture ($unknownCallsite.classification -eq 'RUNTIME_FAIL_FAST_CALLSITE_UNRESOLVED' -and
    $blocked.classification -eq 'TRANSITION_GATE_BLOCKED_SAFE_STOP' -and
    $returned.classification -eq 'INITIALIZEMODULES_RETURNED_MANAGED_ENTRY_NOT_REQUESTED' -and
    $mismatch.classification -eq 'TRANSITION_ATTRIBUTION_INCOMPLETE' -and
    $unlinkedSite.classification -eq 'TRANSITION_ATTRIBUTION_INCOMPLETE') 'transition analyzer rejects unknown, unlinked, or contradictory diagnostic fixtures' 'synthetic'

$results = [ordered]@{
    result='PASS'
    phase='C188R12R4R5R4R5R2R2R4R2R3'
    testRoot=$testRoot
    assertionCount=[int]$fixtureResults.Count
    fixtures=@($fixtureResults.ToArray())
    syntheticEvidenceNotice='Attachment and non-unavailable fatal message rows are synthetic hosted fixtures; they do not establish guest transition acceptance.'
    compiledGateHarness=[ordered]@{
        source=$harnessPath
        sourceSha256=Hash-File $harnessPath
        executable=$testExe
        executableSha256=Hash-File $testExe
        compileLog=$compileLog
        runLog=$testLog
        assertions=($testOutput | Select-String 'PASS ' | Measure-Object).Count
    }
    safeHandoffHostedResults=$safeTestResultsPath
    safeHandoffHostedResultsSha256=Hash-File $safeTestResultsPath
    safeHandoffCaseCount=[int]$safeTestResults.analyzerCaseCount
    transitionAnalyzer=$transitionAnalyzerPath
    transitionAnalyzerSha256=Hash-File $transitionAnalyzerPath
    candidateCallSiteMap=$CallSiteMapPath
    candidateCallSiteMapSha256=Hash-File $CallSiteMapPath
    analyzerSyntheticFixtures=@($attributedFailFast.classification,$unknownCallsite.classification,
        $blocked.classification,$returned.classification,$mismatch.classification,
        $unlinkedSite.classification)
    recoveredSafeStop=[ordered]@{ path=$safeSerial; sha256=$safeSerialHash; stages=29; events=93 }
    priorR4FailFast=[ordered]@{ path=$r4Serial; sha256=$r4SerialHash; result=$r4Reviewed.result; reason=$r4Reviewed.failFast.code }
    productionPersistentCompositeLifecycle=[bool]$prodApplication.persistentCompositeLifecycle
    qemuLaunched=$false
    qacReserved=$false
    managedCollectionRequested=$false
}
$resultsPath = Join-Path $testRoot 'hosted-test-results.json'
[System.IO.File]::WriteAllText($resultsPath,($results | ConvertTo-Json -Depth 8),[System.Text.UTF8Encoding]::new($false))
Write-Output "C188R12R4R5R2R3 transition-attribution tests: PASS ($($fixtureResults.Count) fixtures; $testRoot)"
