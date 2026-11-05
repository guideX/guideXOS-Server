param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [System.IO.Path]::GetFullPath($RepoRoot)
$runId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$testRoot = if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    Join-Path $root "out\dotnet\c188r12r4r5r2r2r4-safe-handoff\hosted-$runId"
} else { [System.IO.Path]::GetFullPath($OutputDirectory) }
if (Test-Path -LiteralPath $testRoot) { throw "Refusing to overwrite hosted evidence: $testRoot" }
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
$analyzer = Join-Path $root 'scripts\dotnet\analyze-c188r12r4r5r2r2r4-safe-handoff.ps1'
$legacyFixture = Join-Path $root 'out\dotnet\c188r12r4r5r4r5r2r2r4-pal-capacity-fix\qac-guest-r2r2r4-001\serial.log'
$legacyAnalysis = Join-Path (Split-Path -Parent $legacyFixture) 'serial-analysis.json'
$legacyFixtureHash = (Get-FileHash -LiteralPath $legacyFixture -Algorithm SHA256).Hash.ToUpperInvariant()
$legacyAnalysisHash = (Get-FileHash -LiteralPath $legacyAnalysis -Algorithm SHA256).Hash.ToUpperInvariant()

function Assert-True([bool]$Value,[string]$Label) {
    if (-not $Value) { throw "C188 safe-handoff assertion failed: $Label" }
}
function Add-Thread([System.Collections.Generic.List[string]]$Lines,[string]$Event,
                    [string]$Raw0='0000000000000001',[string]$Raw1='0000000000000001',
                    [int]$Sequence=-1,[ValidateSet(32,64)][int]$SequenceWidth=64) {
    if ($Sequence -ge 0) {
        $format = if ($SequenceWidth -eq 64) { 'X16' } else { 'X8' }
        $Lines.Add(('[C188R12R4R5R4_THREAD] seq={0} event={1} raw0={2} raw1={3}' -f ($Sequence.ToString($format)), $Event,$Raw0,$Raw1))
    } else {
        $Lines.Add("[C188R12R4R5R4_THREAD] event=$Event raw0=$Raw0 raw1=$Raw1")
    }
}
function Add-CompactStage([System.Collections.Generic.List[string]]$Lines,[string]$Stage,
                          [int]$Begin,[int]$Pass,[int]$Fail,[int]$Observe,
                          [string]$LastResult='00000000',[int]$LastEvent=2,
                          [string]$Singleton='0000001F') {
    $packedCounts = [uint32](($Begin -band 0x7F) -bor (($Pass -band 0x7F) -shl 7) -bor
        (($Fail -band 0x7F) -shl 14) -bor (($Observe -band 0x7F) -shl 21))
    $packedEvent = [uint32](0x80000000 -bor (($LastEvent -band 0x7) -shl 28) -bor $packedCounts)
    $Lines.Add("[C188R12R4R5R1_INIT] invocation=00000001 stage=$Stage event=$('{0:X8}' -f $packedEvent) code=$LastResult singleton=$Singleton")
}
function New-SafeFixture([string]$Name,[object[]]$StageSpecs,[uint32]$Overflow=0,
                         [Nullable[uint32]]$FailureStage=$null,[string]$FailureCode='00000000',
                         [switch]$OmitThreadStore,[switch]$OmitSafeStatus,
                         [switch]$AddTransition,[switch]$AddFailFast,
    [switch]$ContradictOrder,[switch]$DropTraceEnd) {
    $lines = [System.Collections.Generic.List[string]]::new()
    Add-Thread $lines 'adapter_enter' '00000000052ACBD0' '0000000000000000' 0
    $lines.Add('[C188R12R4R5R3_INIT] init_result nativeBool=00000001 adapterStatus=00000000 nativeOutcome=success adapterOutcome=success')
    Add-Thread $lines 'rhinitialize_return' '0000000000000001' '0000000000000000' 1
    Add-Thread $lines 'managed_entry_gate_closed' '0000000000000001' '0000000000000000'
    Add-Thread $lines 'tls_installed' '0000000000001000' '0000000000002000'
    Add-Thread $lines 'runtime_instance' '0000000000003000' '0000000000000001' 2
    if (-not $OmitThreadStore) { Add-Thread $lines 'threadstore' '0000000000004000' '0000000000000001' 3 }
    Add-Thread $lines 'current_thread_tls' '0000000000002000' '0000000000000000' 4
    Add-Thread $lines 'current_thread_state' '0000000000000000' '0000000000000000' 5
    Add-Thread $lines 'module_registration_enter' '0000000000005000' '0000000000006000' 6
    Add-Thread $lines 'module_registration_complete' '0000000000005000' '0000000000000001' 7
    if ($AddTransition) {
        Add-Thread $lines 'module_initialize_enter' '0000000000005000' '0000000000000001' 8
        Add-Thread $lines 'transition_enter' '0000000000002000' '0000000000001000' 9
        Add-Thread $lines 'managed_dispatch' '0000000000007000' '0000000000000001' 10
    }
    if ($AddFailFast) { $lines.Add('[nativeaot-pal-qemu-test] FAIL_FAST reason=50414C06') }

    $specs = @($StageSpecs)
    if ($ContradictOrder -and $specs.Count -gt 2) {
        $specs = @($specs[0],$specs[2],$specs[1]) + @($specs | Select-Object -Skip 3)
    }
    $eventTotal = 0
    $coalesced = 0
    foreach ($spec in $specs) {
        Add-CompactStage $lines $spec.stage $spec.begin $spec.pass $spec.fail $spec.observe $spec.lastResult $spec.lastEvent
        $stageCount = [int]$spec.begin + [int]$spec.pass + [int]$spec.fail + [int]$spec.observe
        $eventTotal += $stageCount
        $coalesced += [Math]::Max(0,$stageCount - 1)
    }
    if ($null -ne $FailureStage) {
        $lines.Add("[C188R12R4R5R1_INIT] invocation=00000001 stage=$('{0:X8}' -f [uint32]$FailureStage) event=80000002 code=$FailureCode singleton=0000001F")
    }
    $eventTotal += [int]$Overflow
    $lines.Add("[C188R12R4R5R1_INIT] invocation=00000001 stage=FFFFFFFF event=80000003 code=$('{0:X8}' -f $eventTotal) singleton=$('{0:X8}' -f $coalesced)")
    if (-not $DropTraceEnd) {
        $lines.Add(('[C188R12R4R5R1_INIT] trace_end invocation=00000001 count={0:X8} overflow={1:X8} serialDropped=00000000 adapterStatus=00000000' -f $specs.Count,$Overflow))
    }
    if (-not $OmitSafeStatus) {
        Add-Thread $lines 'diagnostic_safe_stop' '0000000000000001' '0000000000000000'
        $lines.Add('[nativeaot-safe-handoff] status=intentionally_stopped initialization=accepted moduleRegistration=accepted managedTransition=not_attempted managedEntry=not_reached runtimeError=none')
    }
    $lines.Add('[nativeaot-gc-first-allocation] SAFE_HANDOFF_STOP')
    $serialPath = Join-Path $testRoot "$Name.serial.log"
    $analysisPath = Join-Path $testRoot "$Name.analysis.json"
    [System.IO.File]::WriteAllText($serialPath,(($lines -join "`n") + "`n"),[System.Text.UTF8Encoding]::new($false))
    & pwsh -NoProfile -ExecutionPolicy Bypass -File $analyzer -SerialPath $serialPath -OutputPath $analysisPath | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Safe-handoff analyzer failed for $Name." }
    return [pscustomobject]@{ name=$Name; serial=$serialPath; analysis=(Get-Content -LiteralPath $analysisPath -Raw | ConvertFrom-Json) }
}
function New-Stage([string]$Stage,[int]$Begin=1,[int]$Pass=1,[int]$Fail=0,[int]$Observe=0,
                   [string]$LastResult='00000000',[int]$LastEvent=2) {
    return [pscustomobject]@{ stage=$Stage; begin=$Begin; pass=$Pass; fail=$Fail; observe=$Observe; lastResult=$LastResult; lastEvent=$LastEvent }
}

$fullStages = @(
    (New-Stage '00000001'), (New-Stage '00000002'),
    (New-Stage '00000061'), (New-Stage '00000062')
)

# 1. Successful RhInitialize is independent from module and transition acceptance.
$rhSuccess = New-SafeFixture '01-rhinitialize-success' @((New-Stage '00000001'),(New-Stage '00000002')) -OmitSafeStatus
Assert-True $rhSuccess.analysis.initializationAccepted 'successful RhInitialize is accepted'
Assert-True (-not $rhSuccess.analysis.managedRuntimeAcceptance) 'RhInitialize alone is not managed runtime acceptance'

# 2. Successful module registration does not imply module initialization or managed entry.
$moduleRegistration = New-SafeFixture '02-module-registration-success' $fullStages -OmitSafeStatus
Assert-True $moduleRegistration.analysis.moduleRegistrationAccepted 'module registration is accepted'
Assert-True (-not $moduleRegistration.analysis.managedEntryReached) 'module registration does not claim managed entry'

# 3. Fully ordered diagnostic safe stop is intentional and complete.
$safeStop = New-SafeFixture '03-explicit-safe-stop' $fullStages
Assert-True ($safeStop.analysis.result -eq 'SAFE_HANDOFF_INTENTIONALLY_STOPPED') 'explicit safe stop has its own classification'
Assert-True $safeStop.analysis.managedTransitionIntentionallyNotAttempted 'managed transition is explicitly not attempted'
Assert-True $safeStop.analysis.diagnosticEvidenceComplete 'compact trace counts reconcile with bounded storage'
Assert-True $safeStop.analysis.threadStorePrerequisites.threadStorePublished 'ThreadStore publication is recorded'
Assert-True (-not $safeStop.analysis.threadStorePrerequisites.currentThreadAttached) 'published ThreadStore is not treated as current-thread attachment'
Assert-True (-not $safeStop.analysis.managedRuntimeAcceptance) 'safe stop is not managed runtime acceptance'

function Analyze-SequenceVariant([string]$Name,[string]$SerialText) {
    $serialPath = Join-Path $testRoot "$Name.serial.log"
    $analysisPath = Join-Path $testRoot "$Name.analysis.json"
    [System.IO.File]::WriteAllText($serialPath,$SerialText,[System.Text.UTF8Encoding]::new($false))
    & pwsh -NoProfile -ExecutionPolicy Bypass -File $analyzer -SerialPath $serialPath -OutputPath $analysisPath | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Sequence-marker analyzer failed for $Name." }
    return Get-Content -LiteralPath $analysisPath -Raw | ConvertFrom-Json
}

# The preserved R2R1 producer wrote fixed-width hexadecimal sequence markers.
$archivedR2R1 = Join-Path $root 'out\dotnet\c188r12r4r5r4r5r2r2r4r2-safe-handoff\qac-guest-r2r2r4r2r1-001\serial.log'
$archivedR2R1Hash = (Get-FileHash -LiteralPath $archivedR2R1 -Algorithm SHA256).Hash.ToUpperInvariant()
$archivedR2R1AnalysisPath = Join-Path $testRoot 'sequence-actual-r2r1.analysis.json'
& pwsh -NoProfile -ExecutionPolicy Bypass -File $analyzer -SerialPath $archivedR2R1 -OutputPath $archivedR2R1AnalysisPath | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Archived R2R1 sequence analysis failed.' }
$archivedR2R1Analysis = Get-Content -LiteralPath $archivedR2R1AnalysisPath -Raw | ConvertFrom-Json
Assert-True $archivedR2R1Analysis.threadSequence.complete 'preserved 16-digit guest sequence values are ordered and continuous'
Assert-True ($archivedR2R1Analysis.threadSequence.parsedCount -eq 8) 'all preserved sequenced thread markers are parsed'
Assert-True ($archivedR2R1Analysis.threadSequence.values[0].token -eq '0000000000000000' -and
    $archivedR2R1Analysis.threadSequence.values[7].token -eq '0000000000000007') 'actual zero-padded wire tokens remain unchanged'
Assert-True ((Get-FileHash -LiteralPath $archivedR2R1 -Algorithm SHA256).Hash.ToUpperInvariant() -eq $archivedR2R1Hash) 'sequence analysis preserves the original serial hash'

$safeSequenceText = [System.IO.File]::ReadAllText($safeStop.serial)
$eightDigitText = [regex]::Replace($safeSequenceText,
    '(?<=\bseq=)[0-9A-Fa-f]{16}', [System.Text.RegularExpressions.MatchEvaluator]{
        param($match) $match.Value.Substring(8)
    })
$eightDigit = Analyze-SequenceVariant 'sequence-8-digit' $eightDigitText
Assert-True $eightDigit.threadSequence.complete '8-digit hexadecimal compatibility markers are parsed exactly'
Assert-True ($eightDigit.threadSequence.values[0].widthBits -eq 32) '8-digit wire width is retained as 32 bits'

$maxValidText = $safeSequenceText.Replace('seq=0000000000000000 event=adapter_enter',
    'seq=00000000FFFFFFFF event=adapter_enter')
$maxValid = Analyze-SequenceVariant 'sequence-uint32-maximum' $maxValidText
Assert-True ($maxValid.threadSequence.values[0].token -eq '00000000FFFFFFFF' -and
    [UInt64]$maxValid.threadSequence.values[0].value -eq [UInt32]::MaxValue -and
    $maxValid.threadSequence.values[0].producerDomainValid) 'maximum source-domain value is parsed losslessly'

$highBitText = $safeSequenceText.Replace('seq=0000000000000004 event=current_thread_tls',
    'seq=8000000000000004 event=current_thread_tls')
$highBit = Analyze-SequenceVariant 'sequence-high-bit-uint64' $highBitText
Assert-True ($highBit.threadSequence.values[4].token -eq '8000000000000004' -and
    -not $highBit.threadSequence.values[4].producerDomainValid -and
    -not $highBit.threadSequence.complete) 'high-bit UInt64 is retained exactly and rejected as outside the uint32 producer domain'

$maxUInt64Text = $safeSequenceText.Replace('seq=0000000000000004 event=current_thread_tls',
    'seq=FFFFFFFFFFFFFFFF event=current_thread_tls')
$maxUInt64 = Analyze-SequenceVariant 'sequence-uint64-maximum-wire-value' $maxUInt64Text
Assert-True ($maxUInt64.threadSequence.values[4].token -eq 'FFFFFFFFFFFFFFFF' -and
    -not $maxUInt64.threadSequence.values[4].producerDomainValid) 'maximum 64-bit wire value is not narrowed or accepted as producer-valid'

$malformedSequenceText = $safeSequenceText.Replace('seq=0000000000000003 event=threadstore',
    'seq=G000000000000003 event=threadstore')
$malformedSequence = Analyze-SequenceVariant 'sequence-malformed' $malformedSequenceText
Assert-True ($malformedSequence.threadSequence.malformedCount -gt 0 -and
    -not $malformedSequence.diagnosticEvidenceComplete) 'malformed sequence text fails diagnostic completeness'

$duplicateSequenceText = $safeSequenceText.Replace('seq=0000000000000004 event=current_thread_tls',
    'seq=0000000000000003 event=current_thread_tls')
$duplicateSequence = Analyze-SequenceVariant 'sequence-duplicate' $duplicateSequenceText
Assert-True ($duplicateSequence.threadSequence.duplicateCount -eq 1 -and
    -not $duplicateSequence.diagnosticEvidenceComplete) 'duplicate sequence values are detected'

$outOfOrderSequenceText = $safeSequenceText.Replace('seq=0000000000000004 event=current_thread_tls',
    'seq=0000000000000002 event=current_thread_tls')
$outOfOrderSequence = Analyze-SequenceVariant 'sequence-out-of-order' $outOfOrderSequenceText
Assert-True ($outOfOrderSequence.threadSequence.outOfOrderCount -gt 0 -and
    -not $outOfOrderSequence.diagnosticEvidenceComplete) 'out-of-order sequence values are detected'

$incompleteSequenceText = [regex]::Replace($safeSequenceText,
    '(?m)^\[C188R12R4R5R4_THREAD\] seq=0000000000000004 event=current_thread_tls[^\r\n]*\r?\n', '')
$incompleteSequence = Analyze-SequenceVariant 'sequence-incomplete' $incompleteSequenceText
Assert-True ($incompleteSequence.threadSequence.missingSequenceCount -gt 0 -and
    -not $incompleteSequence.diagnosticEvidenceComplete) 'missing sequence records cannot imply a continuous trace'

# Tooling/result classification stays separate from captured runtime evidence.
$toolResultPath = Join-Path $testRoot 'tooling-failure-result.json'
[System.IO.File]::WriteAllText($toolResultPath,'{"result":"POSTPROCESSING_CLASSIFICATION_FAILURE"}',[System.Text.UTF8Encoding]::new($false))
$toolAnalysisPath = Join-Path $testRoot 'tooling-failure.analysis.json'
& pwsh -NoProfile -ExecutionPolicy Bypass -File $analyzer -SerialPath $safeStop.serial -RunnerResultPath $toolResultPath -OutputPath $toolAnalysisPath | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Tooling-failure classification fixture analysis failed.' }
$toolAnalysis = Get-Content -LiteralPath $toolAnalysisPath -Raw | ConvertFrom-Json
Assert-True $toolAnalysis.toolingFailure 'postprocessing failure is distinguished from runtime status'
Assert-True $toolAnalysis.initializationAccepted 'tooling failure does not erase successful initialization evidence'

# 4. Premature InitializeModules/transition markers invalidate the intentional stop.
$premature = New-SafeFixture '04-premature-module-initialize' $fullStages -AddTransition -OmitSafeStatus
Assert-True $premature.analysis.managedTransitionAttempted 'transition attempt is detected'
Assert-True (-not $premature.analysis.managedTransitionIntentionallyNotAttempted) 'premature transition is not labeled safe'

# 5. The old PAL code remains uninterpreted while transition failure is distinct.
$failFast = New-SafeFixture '05-fail-fast-50414c06' $fullStages -AddTransition -AddFailFast -OmitSafeStatus
Assert-True $failFast.analysis.managedTransitionFailed 'fail-fast after transition entry is a transition failure'
Assert-True ($failFast.analysis.failFast.code -eq '0x50414C06') 'fail-fast numeric code is preserved'
Assert-True ($failFast.analysis.failFast.meaning -match 'uninterpreted') 'no unsupported fail-fast meaning is invented'

# 6. ThreadStore publication is not attachment; absent identity telemetry is reported.
$missingPrereq = New-SafeFixture '06-threadstore-prerequisite-missing' $fullStages -OmitThreadStore
Assert-True (-not $missingPrereq.analysis.threadStorePrerequisites.threadStorePublished) 'missing ThreadStore publication is identified'
Assert-True (-not $missingPrereq.analysis.threadStorePrerequisites.currentThreadAttached) 'current-thread attachment is not inferred from publication'

# 7. A candidate can stop after registration without reaching managed code.
Assert-True ($moduleRegistration.analysis.moduleRegistrationAccepted -and
    -not $moduleRegistration.analysis.managedEntryReached) 'managed entry absence stays explicit after registration'

# 8. Recorder overflow preserves a recorded first failure but makes the full trace incomplete.
$overflowFailureStages = @(
    (New-Stage '00000001'), (New-Stage '00000002'),
    (New-Stage '00000030' 1 0 1 0 '80004005' 3)
)
$overflowFailure = New-SafeFixture '08-overflow-preserved-first-failure' $overflowFailureStages -Overflow 1 -FailureStage 0x30 -FailureCode '80004005'
Assert-True ($overflowFailure.analysis.trace.firstFailure.stage -eq '00000030' -and
    $overflowFailure.analysis.trace.firstFailure.result -eq '80004005') 'first failure survives bounded-record overflow'
Assert-True (-not $overflowFailure.analysis.diagnosticEvidenceComplete) 'overflow prevents complete attribution'

# 9. Overflow without failure attribution is incomplete, never silent success.
$overflowUnknown = New-SafeFixture '09-overflow-without-attribution' $fullStages -Overflow 1 -DropTraceEnd
Assert-True (-not $overflowUnknown.analysis.diagnosticEvidenceComplete) 'overflow without a terminal record stays incomplete'
Assert-True ($null -eq $overflowUnknown.analysis.trace.firstFailure) 'missing failure evidence stays missing'

# 10. A complete bounded successful safe trace has no overflow and no dropped serial.
Assert-True ($safeStop.analysis.trace.eventCount -eq $safeStop.analysis.trace.storedEventCount) 'successful bounded event count is exact'
Assert-True ($safeStop.analysis.trace.overflow -eq 0 -and $safeStop.analysis.trace.serialDropped -eq 0) 'successful bounded trace has no overflow'

# 11. Reordered RhInitialize and module registration stage summaries are contradictory.
$malformed = New-SafeFixture '11-malformed-milestone-order' $fullStages -ContradictOrder
Assert-True (-not $malformed.analysis.milestoneOrderValid) 'contradictory stage order is rejected'
Assert-True ($malformed.analysis.result -eq 'MALFORMED_OR_CONTRADICTORY_MILESTONE_ORDER') 'malformed order gets a distinct classification'

# 12. Analyze the archived R4R1 serial as a separate fixture and preserve both original files.
$legacyOut = Join-Path $testRoot '12-original-r4r1.analysis.json'
& pwsh -NoProfile -ExecutionPolicy Bypass -File $analyzer -SerialPath $legacyFixture -OutputPath $legacyOut | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Original R4R1 serial fixture analysis failed.' }
$legacyResult = Get-Content -LiteralPath $legacyOut -Raw | ConvertFrom-Json
Assert-True $legacyResult.initializationAccepted 'original guest RhInitialize success remains accepted'
Assert-True $legacyResult.moduleRegistrationAccepted 'original guest module registration remains accepted'
Assert-True $legacyResult.managedTransitionFailed 'original guest transition failure is preserved'
Assert-True ($legacyResult.failFast.code -eq '0x50414C06') 'original fail-fast code is preserved'
Assert-True (-not $legacyResult.diagnosticEvidenceComplete) 'original overflow remains evidence incomplete'
Assert-True ((Get-FileHash -LiteralPath $legacyFixture -Algorithm SHA256).Hash.ToUpperInvariant() -eq $legacyFixtureHash) 'archived original raw serial hash is unchanged'
Assert-True ((Get-FileHash -LiteralPath $legacyAnalysis -Algorithm SHA256).Hash.ToUpperInvariant() -eq $legacyAnalysisHash) 'archived original runner analysis hash is unchanged'

$runtimeSource = Get-Content -LiteralPath (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_platform.cpp') -Raw
$traceSource = Get-Content -LiteralPath (Join-Path $root 'tools\dotnet\runtime-pack\src\probes\guidexos_nativeaot_c188r12r4r5_gc_startup_adapter.cpp') -Raw
$kernelSource = Get-Content -LiteralPath (Join-Path $root 'kernel\core\nativeaot_pal_qemu_test.cpp') -Raw
$buildSource = Get-Content -LiteralPath (Join-Path $root 'tools\dotnet\runtime-pack\build-runtime-pack.ps1') -Raw
$managedBuilderSource = Get-Content -LiteralPath (Join-Path $root 'scripts\dotnet\build-managed-hostlog-proof.ps1') -Raw
$productionBuilderSource = Get-Content -LiteralPath (Join-Path $root 'scripts\dotnet\build-c188-production-nativeaot.ps1') -Raw
$productionKernelBuilderSource = Get-Content -LiteralPath (Join-Path $root 'scripts\dotnet\build-c188-production-kernel.ps1') -Raw
$candidateBuilderSource = Get-Content -LiteralPath (Join-Path $root 'scripts\dotnet\build-c188r12r4r5r1-init-attribution-candidate.ps1') -Raw
$runnerSource = Get-Content -LiteralPath (Join-Path $root 'scripts\dotnet\run-c188r12r4r5-gc-ownership-qac-guest.ps1') -Raw
$wrapperSource = Get-Content -LiteralPath (Join-Path $root 'scripts\dotnet\run-c188r12r4r5r2r2r4-safe-handoff-qac-guest.ps1') -Raw
Assert-True ($runtimeSource -match '(?s)RhRegisterOSModule\(.*?g_guideXosNativeAotCodeManagerRegistered = true;.*?if \(registrationOnly\) return;.*?GX_INIT_STAGE_MODULE_INITIALIZE') 'safe return follows actual registration and precedes module initialization'
Assert-True ($runtimeSource -match 'GuideXosNativeAotC188R12R4R5R2R2R4SafeHandoffPrepare\(void\)') 'diagnostic prepare export is explicit'
Assert-True ($runtimeSource -match '(?s)extern "C" __declspec\(noinline\) void __cdecl RhpReversePInvoke\(void\* frame\).*?initializeRuntimeState\(block\);.*?guideXosStockRhpReversePInvoke\(frame\)') 'normal reverse-P/Invoke path remains the pinned transition helper'
Assert-True ($kernelSource -match 'using SafeHandoffPrepare = uint32_t \(GUIDEXOS_NATIVEAOT_PAL_CALL \*\)\(void\)') 'kernel uses the NativeAOT PAL calling convention'
Assert-True ($kernelSource -match 'managed_entry_gate_closed') 'safe mode closes the managed-entry gate'
Assert-True ($traceSource -match 'constexpr uint32_t kInitTraceCapacity = 64u' -and $traceSource -match 'record\.eventCounts\[counterIndex\] >= 0x7Fu' -and $traceSource -match 'packedSummaryEvent') 'trace storage and per-stage event counts remain fixed and bounded'
Assert-True ($buildSource -match 'C188R12R4R5R2R2R4SafeHandoff' -and $buildSource -match 'PersistentCompositeLifecycle') 'build requires isolated opt-in and excludes production lifecycle'
$platformDefinesMatch = [regex]::Match($buildSource, '(?s)\$platformProfileDefines\s*=\s*if \(\$C188R12R4R5R2R2R4SafeHandoff\)\s*\{(?<defines>.*?)\}\s*else')
$platformSafeDefines = if ($platformDefinesMatch.Success) { $platformDefinesMatch.Groups['defines'].Value } else { '' }
Assert-True ($platformSafeDefines.Contains('GUIDEXOS_NATIVEAOT_C188R12R4R5R2R2R4_SAFE_HANDOFF') -and
    $platformSafeDefines.Contains('GUIDEXOS_NATIVEAOT_C188R12R4R5R3_INIT_RETURN_CONTRACT') -and
    $platformSafeDefines.Contains('GUIDEXOS_NATIVEAOT_C188R12R4R5R4R5_GC_INIT_PROBES') -and
    $platformSafeDefines.Contains('GUIDEXOS_NATIVEAOT_C188R12R4R5R4R5R2_SEMISHARED') -and
    $platformSafeDefines.Contains('GUIDEXOS_NATIVEAOT_C188R12R4R5R4R5R2R2_NATIVE_ALLOC') -and
    $buildSource -match '\$threadStoreIncludes \$platformProfileDefines') 'complete safe-profile defines reach the linked platform translation unit'
Assert-True ($managedBuilderSource -match 'C188R12R4R5R2R2R4SafeHandoff\) \{ ''/DGUIDEXOS_NATIVEAOT_C188R12R4R5R2R2R4_SAFE_HANDOFF'' \}\) /I') 'safe profile compiles the compact trace summary emitter into the startup adapter'
Assert-True ($productionBuilderSource.Contains('c188r12r4r5r4r5r2r2r4r2-safe-handoff')) 'proof-disabled production qualification accepts an isolated safe-handoff phase root'
Assert-True ($productionKernelBuilderSource.Contains('c188r12r4r5r4r5r2r2r4r2-safe-handoff')) 'production kernel qualification accepts an isolated safe-handoff phase root'
Assert-True ($candidateBuilderSource -match '--start-address=\$startArgument' -and
    $candidateBuilderSource -match '--stop-address=\$stopArgument' -and
    $candidateBuilderSource -match 'GuideXosNativeAotC188R12R4R5R2R2R4SafeHandoffPrepare' -and
    $candidateBuilderSource -match 'initializeRuntimeState' -and
    $candidateBuilderSource -match '22runFirstRealAllocationEPKh' -and
    $candidateBuilderSource -match 'kernelSafeHandoffDispatch' -and
    $candidateBuilderSource -match '\[0-9a-fA-F\]\{1,16\}') 'candidate retains exact map-address-bounded linked runtime and kernel dispatch disassembly'
Assert-True ($candidateBuilderSource -match 'safe-handoff-hosted-tests-resume-\{0\}' -and
    $candidateBuilderSource -match '\$safeHostedTestLog') 'resumed candidate preserves earlier hosted evidence and writes to unique paths'
Assert-True ($runnerSource -match 'Test-QacClearWithCensus' -and $runnerSource -match "Invoke-Qmp 'quit'" -and
    $runnerSource -match 'serialSha256BeforeAnalysis' -and $runnerSource -match 'serialSha256AfterAnalysis') 'runner retains QAC admission, owned shutdown, and raw-serial preservation checks'
Assert-True ($runnerSource -match 'SAFE_HANDOFF_GUEST_QUALIFIED' -and $runnerSource -match 'diagnosticEvidenceComplete' -and
    $runnerSource -match 'MANAGED_TRANSITION_FAILED' -and $runnerSource -match 'QAC_RELEASE') 'runner separates safe qualification, transition failure, and evidence incompleteness'
Assert-True ($wrapperSource -match 'NativeAotSafeHandoff' -and $wrapperSource -match 'R2R2R4' -and
    $wrapperSource -match 'run-c188r12r4r5-gc-ownership-qac-guest\.ps1') 'next-phase wrapper routes to the guarded one-boot QAC runner'

$result = [ordered]@{
    result='PASS'; testRoot=$testRoot; analyzer=$analyzer
    analyzerCaseCount=22
    analyzerCases=@(
        'RhInitialize success';'module registration success';'explicit diagnostic safe stop'
        'premature InitializeModules transition';'fail-fast 0x50414C06';'ThreadStore prerequisites missing'
        'managed entry not reached';'record overflow with first failure preserved'
        'record overflow without complete attribution';'full successful bounded diagnostic trace'
        'malformed or contradictory milestone ordering';'original R4R1 raw guest fixture'
        'tooling or postprocessing failure distinct from runtime status'
        '8-digit sequence wire parsing';'16-digit preserved guest sequences';'leading-zero preservation'
        'uint32 source-domain maximum';'high-bit UInt64 detection without narrowing'
        'maximum UInt64 wire-value detection';'malformed sequence marker rejection'
        'duplicate sequence rejection';'out-of-order sequence rejection';'incomplete sequence rejection'
    )
    sourceContractChecks=10
    qemuLaunched=$false; qacReserved=$false
    legacyRawSerialSha256Before=$legacyFixtureHash
    legacyRawSerialSha256After=(Get-FileHash -LiteralPath $legacyFixture -Algorithm SHA256).Hash.ToUpperInvariant()
    legacyRunnerAnalysisSha256Before=$legacyAnalysisHash
    legacyRunnerAnalysisSha256After=(Get-FileHash -LiteralPath $legacyAnalysis -Algorithm SHA256).Hash.ToUpperInvariant()
    classifications=[ordered]@{
        successfulInitialization=$rhSuccess.analysis.initializationAccepted
        successfulModuleRegistration=$moduleRegistration.analysis.moduleRegistrationAccepted
        intentionalStop=$safeStop.analysis.result
        actualTransitionFailure=$failFast.analysis.result
        originalR4R1=$legacyResult.result
    }
}
$resultPath = Join-Path $testRoot 'hosted-test-results.json'
[System.IO.File]::WriteAllText($resultPath,($result | ConvertTo-Json -Depth 8),[System.Text.UTF8Encoding]::new($false))
Write-Output "C188R12R4R5R2R2R4 safe-handoff tests: PASS ($testRoot)"
