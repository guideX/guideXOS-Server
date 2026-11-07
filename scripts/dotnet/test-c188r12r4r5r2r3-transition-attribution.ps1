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
$palBuilderPath = Join-Path $root 'tools\dotnet\runtime-pack\build-guidexos-nativeaot-pal.ps1'
$kernelPalPath = Join-Path $root 'kernel\core\nativeaot_pal_qemu_test.cpp'
$candidateBuilderPath = Join-Path $root 'scripts\dotnet\build-c188r12r4r5r1-init-attribution-candidate.ps1'
$managedBuilderPath = Join-Path $root 'scripts\dotnet\build-managed-hostlog-proof.ps1'
$productionNativeBuilderPath = Join-Path $root 'scripts\dotnet\build-c188-production-nativeaot.ps1'
$productionKernelBuilderPath = Join-Path $root 'scripts\dotnet\build-c188-production-kernel.ps1'
$runnerPath = Join-Path $root 'scripts\dotnet\run-c188r12r4r5-gc-ownership-qac-guest.ps1'
$runnerContractPath = Join-Path $root 'scripts\dotnet\test-c188r12r4r5r2r3-transition-runner-contract.ps1'
$runtimeBuilderPath = Join-Path $root 'tools\dotnet\runtime-pack\build-runtime-pack.ps1'
$attachmentContractPath = Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_thread_attachment_contract.h'
$runtimeLockPath = Join-Path $root 'tools\dotnet\runtime-pack\runtime-pack.lock.json'
$analyzerPath = Join-Path $root 'scripts\dotnet\analyze-c188r12r4r5r2r2r4-safe-handoff.ps1'
$transitionAnalyzerPath = Join-Path $root 'scripts\dotnet\analyze-c188r12r4r5r2r3-transition-attribution.ps1'
$safeTestsPath = Join-Path $root 'scripts\dotnet\test-c188r12r4r5r2r2r4-safe-handoff.ps1'
$harnessPath = Join-Path $root 'tests\dotnet\runtime-pack\c188r12r4r5r2r3_transition_attribution_test.cpp'
foreach ($path in @($headerPath,$platformPath,$palPath,$palBuilderPath,$kernelPalPath,$candidateBuilderPath,$runtimeBuilderPath,
        $managedBuilderPath,$productionNativeBuilderPath,$productionKernelBuilderPath,$runnerPath,$runnerContractPath,
        $attachmentContractPath,$runtimeLockPath,$analyzerPath,$transitionAnalyzerPath,$safeTestsPath,$harnessPath,$CallSiteMapPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required transition fixture input missing: $path" }
}

$header = Get-Content -LiteralPath $headerPath -Raw
$platform = Get-Content -LiteralPath $platformPath -Raw
$pal = Get-Content -LiteralPath $palPath -Raw
$palBuilder = Get-Content -LiteralPath $palBuilderPath -Raw
$kernelPal = Get-Content -LiteralPath $kernelPalPath -Raw
$candidateBuilder = Get-Content -LiteralPath $candidateBuilderPath -Raw
$managedBuilderSource = Get-Content -LiteralPath $managedBuilderPath -Raw
$productionNativeBuilder = Get-Content -LiteralPath $productionNativeBuilderPath -Raw
$productionKernelBuilder = Get-Content -LiteralPath $productionKernelBuilderPath -Raw
$runnerSource = Get-Content -LiteralPath $runnerPath -Raw
$runnerContractSource = Get-Content -LiteralPath $runnerContractPath -Raw
$runtimeBuilder = Get-Content -LiteralPath $runtimeBuilderPath -Raw
$attachmentContract = Get-Content -LiteralPath $attachmentContractPath -Raw
$runtimeLock = Get-Content -LiteralPath $runtimeLockPath -Raw | ConvertFrom-Json -ErrorAction Stop
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
$r4BridgeRun = Join-Path $root 'out\dotnet\c188r12r4r5r4r5r2r2r4r2r3-transition-attribution\qac-guest-transition-attribution-C188R12R4R5R2R3R1-20261107T135044173Z-d2e925c535474d88a45e0eb60a5dbde8'
$r4BridgeSerial = Join-Path $r4BridgeRun 'serial.log'
$r4CandidateManifestPath = Join-Path $root 'out\dotnet\c188r12r4r5r4r5r2r2r4r2r3-transition-attribution\candidate-20261107T123816299Z\candidate-manifest.json'
foreach ($path in @($safeSerial,$safeReviewedPath,$r4Serial,$r4ReviewedPath,$r4BridgeSerial,$r4CandidateManifestPath,
        $ProductionNativeAotManifest,$ProductionNativeAotMap,$ProductionApplicationManifest)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Preserved or production evidence missing: $path" }
}

$safeSerialHash = Hash-File $safeSerial
$r4SerialHash = Hash-File $r4Serial
$r4BridgeSerialHash = Hash-File $r4BridgeSerial
$safeReviewed = Get-Content -LiteralPath $safeReviewedPath -Raw | ConvertFrom-Json
$r4Reviewed = Get-Content -LiteralPath $r4ReviewedPath -Raw | ConvertFrom-Json
$r4Candidate = Get-Content -LiteralPath $r4CandidateManifestPath -Raw | ConvertFrom-Json
$r4RuntimeManifest = Get-Content -LiteralPath $r4Candidate.runtimePackManifest -Raw | ConvertFrom-Json
$r4RuntimeSourceRoot = [string]$r4RuntimeManifest.c188R12R4R5R4ThreadStoreReadinessSourceRoot
$r4AsmOffsetsPath = Join-Path $r4RuntimeSourceRoot 'src\coreclr\nativeaot\Runtime\AsmOffsets.h'
$r4ThreadHeaderPath = Join-Path $r4RuntimeSourceRoot 'src\coreclr\nativeaot\Runtime\thread.h'
if (-not (Test-Path -LiteralPath $r4AsmOffsetsPath -PathType Leaf)) { throw "Pinned Thread layout source missing: $r4AsmOffsetsPath" }
if (-not (Test-Path -LiteralPath $r4ThreadHeaderPath -PathType Leaf)) { throw "Pinned Thread structure source missing: $r4ThreadHeaderPath" }
$r4AsmOffsets = Get-Content -LiteralPath $r4AsmOffsetsPath -Raw
$r4ThreadHeader = Get-Content -LiteralPath $r4ThreadHeaderPath -Raw
$prodRuntime = Get-Content -LiteralPath $ProductionNativeAotManifest -Raw | ConvertFrom-Json
$prodApplication = Get-Content -LiteralPath $ProductionApplicationManifest -Raw | ConvertFrom-Json
$prodMap = Get-Content -LiteralPath $ProductionNativeAotMap -Raw

$r4BridgeLines = @(Get-Content -LiteralPath $r4BridgeSerial)
$r4BridgeEvents = [System.Collections.Generic.List[object]]::new()
foreach ($line in $r4BridgeLines) {
    $match = [regex]::Match($line,'C188R12R4R5R4_FLS\] seq=(?<seq>[0-9A-Fa-f]{8}) event=(?<event>\S+) raw0=(?<raw0>[0-9A-Fa-f]{16}) raw1=(?<raw1>[0-9A-Fa-f]{16})')
    if ($match.Success) {
        $r4BridgeEvents.Add([ordered]@{
            sequence=[Convert]::ToUInt32($match.Groups['seq'].Value,16)
            event=$match.Groups['event'].Value
            raw0=$match.Groups['raw0'].Value.ToUpperInvariant()
            raw1=$match.Groups['raw1'].Value.ToUpperInvariant()
        })
    }
}
$r4BridgeEventNames = @($r4BridgeEvents | ForEach-Object { $_.event })
$r4BridgeSequences = @($r4BridgeEvents | ForEach-Object { [int]$_.sequence })
$r4BridgeStateLine = $r4BridgeLines | Where-Object { $_ -match '^\[C188R12R4R5R2R3_STATE\]' } | Select-Object -Last 1
$r4BridgeTlsLine = $r4BridgeLines | Where-Object { $_ -match '^\[nativeaot-gc-first-allocation\].*tlsBlock=' } | Select-Object -First 1
$r4BridgeTlsMatch = [regex]::Match([string]$r4BridgeTlsLine,'tlsBlock=(?<block>[0-9A-Fa-f]{16})')
$r4BridgeThreadMatch = [regex]::Match([string]$r4BridgeStateLine,'rawThread=(?<thread>[0-9A-Fa-f]{16})')
$r4BridgeThreadIdMatch = [regex]::Match([string]$r4BridgeStateLine,'nativeThreadId=(?<threadId>[0-9A-Fa-f]{16})')
$r4BridgeOsIdMatch = [regex]::Match([string]$r4BridgeStateLine,'observedOsThreadId=(?<osId>[0-9A-Fa-f]{16})')
$r4BridgeNativeId = if ($r4BridgeThreadIdMatch.Success) { $r4BridgeThreadIdMatch.Groups['threadId'].Value.ToUpperInvariant() } else { '' }
$r4BridgeOsId = if ($r4BridgeOsIdMatch.Success) { $r4BridgeOsIdMatch.Groups['osId'].Value.ToUpperInvariant() } else { '' }
$r4BridgeTlsBlock = [UInt64]0
$r4BridgeRawThread = [UInt64]0
if ($r4BridgeTlsMatch.Success) { $r4BridgeTlsBlock = [Convert]::ToUInt64($r4BridgeTlsMatch.Groups['block'].Value,16) }
if ($r4BridgeThreadMatch.Success) { $r4BridgeRawThread = [Convert]::ToUInt64($r4BridgeThreadMatch.Groups['thread'].Value,16) }
$r4BridgeOverflow = @($r4BridgeLines | Where-Object { $_ -match 'pal_fls_trace_overflow|initOverflow=00000001|threadOverflow=00000001' }).Count -gt 0

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
Assert-Fixture (($testOutput -join "`n") -match 'raw Thread present but uninitialized is ready for stock attachment') 'raw Thread pointer is distinct from initialized Thread state' 'synthetic gate fixture'
Assert-Fixture (($testOutput -join "`n") -match 'current ThreadStore thread with empty PAL FLS blocks at gate 6' -and
    ($testOutput -join "`n") -match 'current ThreadStore thread with foreign PAL FLS blocks at gate 6') 'attached current thread rejects absent or foreign PAL FLS' 'synthetic gate fixture'
Assert-Fixture (($testOutput -join "`n") -match 'purported attached flag with null PAL FLS blocks at gate 6' -and
    ($testOutput -join "`n") -match 'PAL FLS populated before ThreadStore publication blocks at gate 6') 'FLS and attached-state publication must agree in either direction' 'synthetic gate fixture'
Assert-Fixture (($testOutput -join "`n") -match 'initialized but unpublished Thread blocks stock AttachCurrentThread early return') 'initialized but unpublished Thread cannot enter stock attach early return' 'synthetic gate fixture'
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
Assert-Fixture (($testOutput -join "`n") -match 'raw Thread present but uninitialized is ready for stock attachment' -and
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
Assert-Fixture ($r4Candidate.runtimePackSourceCommit -eq '9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3' -and
    $r4RuntimeManifest.platformObject.runtimeCellOffset -eq '0x30' -and
    $r4AsmOffsets -match 'ASM_OFFSET\(\s*28,\s*38,\s*Thread,\s*m_ThreadStateFlags\)' -and
    -not $platform.Contains('kRuntimeCellInitializedOffset') -and
    $platform.Contains('GuideXosTransitionInitializeLegacyRuntimeCell') -and
    $attachmentContract.Contains('if (cell == nullptr || *cell != nullptr) return false;') -and
    $runtimeLock.platformObject.PSObject.Properties['runtimeCellInitializedOffset'] -eq $null -and
    $runtimeLock.platformObject.PSObject.Properties['flsCellOffset'] -eq $null) 'R5 TLS marker alias is removed; shared idempotent sentinel touches only its runtime cell'
Assert-Fixture (-not $platform.Contains('flsCell(') -and -not $platform.Contains('kFlsCellOffset') -and
    $platform.Contains('guidexos_nativeaot_pal_fls_get(index)') -and
    $platform.Contains('guidexos_nativeaot_pal_fls_set(index, value)') -and
    $platform -match 'record\.runtimeFlsCellReady\s*=\s*block\s*!=\s*nullptr\s*&&\s*record\.palFlsIndex\s*<\s*kFlsCellCount' -and
    $r4ThreadHeader.Contains('PInvokeTransitionFrame* m_pCachedTransitionFrame') -and
    $r4AsmOffsets -match 'ASM_OFFSET\(\s*30,\s*48,\s*Thread,\s*m_pDeferredTransitionFrame\)') 'platform FLS storage no longer overlays Thread fields and direct FLS shares PAL LocalStorage ownership'
Assert-Fixture ($platform -match 'offsetof\(RuntimeThreadLocals,\s*m_ThreadStateFlags\)\s*==\s*0x38u' -and
    $platform -match 'offsetof\(RuntimeThreadLocals,\s*m_pTransitionFrame\)\s*==\s*0x40u' -and
    $platform -match 'offsetof\(RuntimeThreadLocals,\s*m_pDeferredTransitionFrame\)\s*==\s*0x48u' -and
    $platform -match 'offsetof\(RuntimeThreadLocals,\s*m_pCachedTransitionFrame\)\s*==\s*0x50u' -and
    $platform -match 'offsetof\(RuntimeThreadLocals,\s*m_pNext\)\s*==\s*0x58u' -and
    $platform -match 'offsetof\(RuntimeThreadLocals,\s*m_hPalThread\)\s*==\s*0x60u' -and
    $runtimeBuilder.Contains('c188r12r4r5r2r3ThreadLayoutEvidence')) 'candidate compilation asserts pinned Thread ABI and preserves source-hash layout evidence'
Assert-Fixture ($attachmentContract.Contains('GX_TRANSITION_PAL_FLS_UNAVAILABLE') -and
    $attachmentContract.Contains('if (!flsIndexReady)') -and
    $attachmentContract.Contains('flsValue == 0u') -and
    $attachmentContract.Contains('flsValue == rawCurrentThread') -and
    $platform.Contains('GuideXosTransitionClassifyPalFls(') -and
    $platform.Contains('GuideXosTransitionClassifyAttachment(stateFlags)')) 'diagnostic distinguishes absent attachment, unavailable or empty PAL FLS, matching FLS, and stale foreign FLS'
Assert-Fixture ($platform.Contains('GX_THREADSTORE_EVENT_THREADSTORE_PUBLICATION') -and
    $platform.Contains('ThreadStore::Iterator iterator') -and
    $platform.Contains('iterator.GetNext()') -and
    $platform.Contains('currentStore->LockThreadStore()') -and
    $platform.Contains('threadStorePublished ? 1u : 0u') -and
    $platform.Contains('!threadStorePublished')) 'native publication marker verifies current Thread membership in the actual ThreadStore list'
Assert-Fixture ($platform -match 'kThreadReadinessMarkerCapacity\s*=\s*64u' -and
    $runtimeBuilder -match 'c188r12r4r5r4MarkerCapacity\s*=.*\{\s*64\s*\}' -and
    $platform.Contains('GX_THREADSTORE_EVENT_THREADSTORE_PUBLICATION')) 'bounded readiness trace capacity and manifest include the ThreadStore publication marker'
Assert-Fixture ($candidateBuilder -match '\[string\]\$ArtifactRoot' -and
    $candidateBuilder.Contains('-ArtifactRoot') -and
    $candidateBuilder.Contains('must remain below ArtifactRoot') -and
    $candidateBuilder.Contains('BUILD_DIR=$($kernelBuildDir.Replace') -and
    $productionNativeBuilder -match '\[string\]\$ArtifactRoot' -and
    $productionKernelBuilder -match '\[string\]\$ArtifactRoot' -and
    $managedBuilderSource.Contains('$managedArtifactRoot = Join-Path $OutputRoot "artifacts"') -and
    $managedBuilderSource.Contains('Assert-WithinRoot $DirectoryBuildPropsPath $ArtifactRoot') -and
    $runnerSource -match '\[string\]\$ArtifactRoot' -and $runnerContractSource.Contains('-ArtifactRoot $artifactRoot')) 'fresh candidate and runner support isolated artifact roots while preserving candidate containment'
Assert-Fixture ($palAttachBody.Success -and
    $palAttachBody.Groups['body'].Value.Contains('#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R4_THREADSTORE_READINESS)') -and
    $palBuilder.Contains('$commonDefines') -and
    -not $palBuilder.Contains('$commonDefines /DGUIDEXOS_NATIVEAOT_C188R12R4R5R4_THREADSTORE_READINESS')) 'missing conditional PalAttach marker alone remains inconclusive'
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

$expectedR4BridgeEvents = @('pal_fls_allocate','pal_fls_get_context','pal_fls_get_result',
    'pal_fls_get_storage','pal_fls_get_cell','pal_fls_get_owner','pal_fls_get_value')
$r4BridgeOrderValid = ($r4BridgeSequences -join ',') -eq '0,1,2,3,4,5,6' -and
    ($r4BridgeEventNames -join ',') -eq ($expectedR4BridgeEvents -join ',')
$r4BridgeSetEvents = @($r4BridgeEventNames | Where-Object { $_ -like 'pal_fls_set_*' }).Count
$r4PalAttachMarkers = @($r4BridgeLines | Where-Object { $_ -match 'event=pal_attach_(enter|existing|complete)' }).Count
$r4GetResult = @($r4BridgeEvents | Where-Object { $_.sequence -eq 2u }) | Select-Object -First 1
$r4GetStorage = @($r4BridgeEvents | Where-Object { $_.sequence -eq 3u }) | Select-Object -First 1
$r4RuntimeCellOffset = [Convert]::ToUInt64(([string]$r4RuntimeManifest.platformObject.runtimeCellOffset -replace '^0x',''),16)
$r4OldInitMarker = [Convert]::ToUInt64(([string]$r4RuntimeManifest.platformObject.runtimeCellInitializedOffset -replace '^0x',''),16)
$r4ThreadFlagsOffset = [UInt64]0x38
$r4TlsAliasProven = $r4BridgeTlsBlock -ne 0 -and $r4BridgeRawThread -eq ($r4BridgeTlsBlock + $r4RuntimeCellOffset) -and
    ($r4BridgeTlsBlock + $r4RuntimeCellOffset + $r4OldInitMarker) -eq ($r4BridgeRawThread + $r4ThreadFlagsOffset)
Assert-Fixture ($r4BridgeEvents.Count -eq 7 -and $r4BridgeOrderValid -and -not $r4BridgeOverflow -and
    $r4Candidate.threadStoreTelemetryStackCapacity -eq 8) 'R4 bridge trace event order is complete with no overflow' 'preserved native serial'
Assert-Fixture ($r4PalAttachMarkers -eq 0 -and $r4BridgeSetEvents -eq 0 -and
    $r4BridgeEvents.Count -lt 32 -and
    $palAttachBody.Groups['body'].Value.Contains('#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R4_THREADSTORE_READINESS)')) 'missing conditional attach marker is inconclusive; bounded active bridge records no FLS setter' 'preserved native serial and source'
Assert-Fixture ($r4GetResult.raw0 -eq '0000000000000000' -and
    $r4GetResult.raw1 -eq '0000000100000001' -and $r4GetStorage.raw0 -eq $r4GetResult.raw1 -and
    $r4GetStorage.raw1 -eq '0000000000000000') 'LocalStorage get succeeds for the allocated slot and generation while its value is null' 'preserved native serial'
Assert-Fixture ($r4TlsAliasProven -and $r4BridgeStateLine -match 'attachment=00000002' -and
    $r4BridgeStateLine -match 'palFlsValue=0000000000000000 flsState=00000000' -and
    $r4BridgeNativeId -eq '0000000000000000' -and $r4BridgeOsId -eq '0000000000000001') 'R4 attached bit aliases the startup marker; zero native ID and frame indicate construction did not run' 'preserved native serial and pinned source'

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
    r4PalFlsBridgeTrace=[ordered]@{
        path=$r4BridgeSerial
        sha256=$r4BridgeSerialHash
        eventCount=$r4BridgeEvents.Count
        eventSequences=@($r4BridgeSequences)
        eventNames=@($r4BridgeEventNames)
        overflow=$r4BridgeOverflow
        setterEventCount=$r4BridgeSetEvents
        localStorageResult=$r4GetResult.raw0
        localStorageIdentity=$r4GetResult.raw1
        storedMarker=$r4GetStorage.raw1
        tlsThreadOffsetAliasProven=$r4TlsAliasProven
    }
    productionPersistentCompositeLifecycle=[bool]$prodApplication.persistentCompositeLifecycle
    qemuLaunched=$false
    qacReserved=$false
    managedCollectionRequested=$false
}
$resultsPath = Join-Path $testRoot 'hosted-test-results.json'
[System.IO.File]::WriteAllText($resultsPath,($results | ConvertTo-Json -Depth 8),[System.Text.UTF8Encoding]::new($false))
Write-Output "C188R12R4R5R2R3 transition-attribution tests: PASS ($($fixtureResults.Count) fixtures; $testRoot)"
