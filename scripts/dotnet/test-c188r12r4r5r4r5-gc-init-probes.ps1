param([string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [System.IO.Path]::GetFullPath($RepoRoot)
$stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$testRoot = Join-Path $root "out\dotnet\c188r12r4r5r4r5-gc-init-probes\hosted-$stamp"
if (Test-Path -LiteralPath $testRoot) { throw "Refusing to overwrite hosted evidence: $testRoot" }
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
$analyzer = Join-Path $root 'scripts\dotnet\analyze-c188r12r4r5r1-init-serial.ps1'
$recordHeader = Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_gcinit_record.h'
$recordSource = Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_gcinit_record.cpp'
$recordTest = Join-Path $root 'tools\dotnet\runtime-pack\tests\c188r12r4r5r4r5_gc_init_record_test.cpp'
$runtimeBuild = Join-Path $root 'tools\dotnet\runtime-pack\build-runtime-pack.ps1'
$adapterSource = Join-Path $root 'tools\dotnet\runtime-pack\src\probes\guidexos_nativeaot_c188r12r4r5_gc_startup_adapter.cpp'
$kernelSource = Join-Path $root 'kernel\core\nativeaot_pal_qemu_test.cpp'
$guestRunner = Join-Path $root 'scripts\dotnet\run-c188r12r4r5-gc-ownership-qac-guest.ps1'
$pinnedGcSource = Join-Path $root 'out\dotnet\gc-feasibility-baseline\nativeaot-runtime\src\coreclr\gc\gc.cpp'
$r3Serial = Join-Path $root 'out\dotnet\c188r12r4r5r4-threadstore-readiness\candidate-20261031T174601065Z\qac-guest-003\serial.log'

function Assert-True([bool]$Value,[string]$Label) {
    if (-not $Value) { throw "C188R12R4R5R4R5 assertion failed: $Label" }
}
function Hash-File([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}
function Invoke-Trace([string]$Name,[string[]]$Lines) {
    $serialPath = Join-Path $testRoot "$Name.serial.log"
    $analysisPath = Join-Path $testRoot "$Name.analysis.json"
    [System.IO.File]::WriteAllText($serialPath, (($Lines -join "`n") + "`n"), [System.Text.UTF8Encoding]::new($false))
    & pwsh -NoProfile -ExecutionPolicy Bypass -File $analyzer -SerialPath $serialPath -OutputPath $analysisPath | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "GC-init trace analyzer failed for $Name." }
    return Get-Content -LiteralPath $analysisPath -Raw | ConvertFrom-Json
}
function New-GcInitRecordLines([hashtable]$Fields,[uint32]$Invocation=1) {
    $records = [string[]]::new(16)
    foreach ($field in 0..15) {
        $records[$field] = '[C188R12R4R5R4R5_GCINIT] invocation={0:X8} field={1:X8} value={2:X8}' -f
            $Invocation,$field,[uint32]$Fields[$field]
    }
    return ,$records
}
function New-Record([uint32]$HeapState,[uint32]$HeapHr,[uint32]$InitializeGcHr,
                    [uint32]$SemiState,[uint32]$SemiResult,
                    [uint32]$WaitAllocState,[uint32]$WaitAllocResult,
                    [uint32]$WaitCreateState,[uint32]$WaitCreateResult,
                    [uint32]$FailureSite) {
    return @{
        0=[uint32]0x43385235; 1=[uint32]1; 2=[uint32]1; 3=$HeapState
        4=$HeapHr; 5=$InitializeGcHr; 6=$SemiState; 7=$SemiResult
        8=$WaitAllocState; 9=$WaitAllocResult; 10=$WaitCreateState
        11=$WaitCreateResult; 12=$FailureSite; 13=[uint32]::MaxValue
        14=[uint32]0; 15=[uint32]0
    }
}
function New-OuterLines([bool]$Failure) {
    $lines = [System.Collections.Generic.List[string]]::new()
    $event = if ($Failure) { '00000003' } else { '00000002' }
    $code = if ($Failure) { '80004005' } else { '00000000' }
    $lines.Add("[C188R12R4R5R1_INIT] invocation=00000001 stage=00000032 event=$event code=$code singleton=0000001F")
    return ,$lines
}

foreach ($path in @($analyzer,$recordHeader,$recordSource,$recordTest,$runtimeBuild,$adapterSource,$kernelSource,$guestRunner,$pinnedGcSource,$r3Serial)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required GC-init qualification input is missing: $path" }
}

$compiler = Get-Command 'g++.exe' -ErrorAction SilentlyContinue
if ($null -eq $compiler) { $compiler = Get-Command 'clang++.exe' -ErrorAction SilentlyContinue }
if ($null -eq $compiler) { throw 'A hosted C++ compiler (g++ or clang++) is required to exercise the diagnostic record implementation.' }
$recordExe = Join-Path $testRoot 'gc-init-record-test.exe'
& $compiler.Source '-std=c++17' '-Wall' '-Wextra' '-Werror' '-DGUIDEXOS_NATIVEAOT_C188R12R4R5R4R5_GC_INIT_PROBES' `
    $recordSource $recordTest '-o' $recordExe
if ($LASTEXITCODE -ne 0) { throw 'The bounded C188 GC-init record hosted test did not compile.' }
& $recordExe
if ($LASTEXITCODE -ne 0) { throw 'The bounded C188 GC-init record implementation failed its hosted assertions.' }

$gcText = Get-Content -LiteralPath $pinnedGcSource -Raw
$buildText = Get-Content -LiteralPath $runtimeBuild -Raw
$adapterText = Get-Content -LiteralPath $adapterSource -Raw
$kernelText = Get-Content -LiteralPath $kernelSource -Raw
$guestRunnerText = Get-Content -LiteralPath $guestRunner -Raw
Assert-True ($gcText.Contains('if (!init_semi_shared())') -and $gcText.Contains('WaitForGCEvent->CreateManualEventNoThrow(TRUE)')) 'pinned implementation contains both audited operation boundaries'
Assert-True ($buildText.Contains('gcwks.cpp.obj') -and $buildText.Contains('C188R12R4R5R4R5_GC_INIT_PROBES')) 'build recipe replaces the linked Workstation GC member only for the diagnostic profile'
Assert-True ($adapterText.Contains('GuideXosNativeAotGcInitGetField') -and $adapterText.Contains('GuideXosNativeAotGcInitFinalizeForRead();') -and $adapterText.Contains('emitGcInitRecord();')) 'adapter finalizes and reads the record after RhInitialize returns'
Assert-True ($kernelText.Contains('C188R12R4R5R4R5_GCINIT') -and $kernelText.Contains('GX_INIT_STAGE_GC_INTERNAL_RECORD')) 'serial sink emits the bounded GCINIT marker family'
Assert-True (-not (Select-String -LiteralPath $recordSource -Pattern '\b(new|malloc|calloc|realloc|printf|puts|CreateEvent|GetLastError)\b' -Quiet)) 'record implementation contains no allocator, logger, or Windows/PAL service call'
Assert-True ($guestRunnerText.Contains("'GcInitFailureAttribution','SemiSharedAttribution','NativeAllocationFailureAttribution','NativeAotSafeHandoff'") -and
    $guestRunnerText.Contains('PASS_BUILD_READY_FOR_GC_INIT_ATTRIBUTION_GUEST') -and
    $guestRunnerText.Contains('PASS_BUILD_READY_FOR_SEMISHARED_ATTRIBUTION_GUEST')) 'QAC runner accepts the qualified R4R5 and R2 diagnostic candidate profiles'
Assert-True ($guestRunnerText.Contains('qac-guest-r4r5r1-001') -and $guestRunnerText.Contains('guest-launch-attempt-C188R12R4R5R4R5R1.json')) 'R4R5R1 uses a distinct guest directory and one-boot guard'
Assert-True ($guestRunnerText.Contains('if ($isGcInitFailureAttribution -or $isSafeHandoff) { Join-Path $phaseRoot $runDirectory }')) 'R4R5R1 and safe-handoff guest evidence stay outside the qualified candidate tree'
Assert-True ($guestRunnerText.Contains('[System.IO.FileMode]::CreateNew') -and $guestRunnerText.Contains('maxGuestBoots=1;automaticRetry=$false')) 'R4R5R1 consumes its guard only after successful preflight and forbids retries'
Assert-True ($guestRunnerText.Contains('Test-QacClearWithCensus -Snapshot $before') -and $guestRunnerText.Contains('Get-WindowsQemuProcessCensus')) 'R4R5R1 requires complete QAC and Windows QEMU admission evidence'
Assert-True ($guestRunnerText.Contains('c188r12r4r5r4r5GcInitRecordObject') -and $guestRunnerText.Contains('gcinit-workstation-source') -and $guestRunnerText.Contains('instrumented Workstation GC object')) 'R4R5R1 rechecks pinned and instrumented GC source/object identities before admission'
Assert-True ($guestRunnerText.Contains('analyze-c188r12r4r5r1-init-serial.ps1') -and $guestRunnerText.Contains('analyze-c188r12r4r5r4-threadstore-readiness.ps1')) 'R4R5R1 preserves separate GC-init and ThreadStore analyses'
Assert-True ($guestRunnerText.Contains('Test-ModuleRegistrationStageObserved $serialAnalysis') -and
    $guestRunnerText.Contains('moduleRegistrationAccepted')) 'R4R5R1 reports observed registration and the safe profile consumes its explicit registration milestone'
Assert-True ($guestRunnerText.Contains('GC_INIT_ROOT_CAUSE_PROVEN') -and $guestRunnerText.Contains('DIAGNOSTIC_INCONCLUSIVE')) 'explicit internal failures and missing or incomplete records receive distinct runner outcomes'

$eFail = [Convert]::ToUInt32('80004005',16)
$eOutOfMemory = [Convert]::ToUInt32('8007000E',16)
$successRecord = New-Record 2 0 0 2 1 2 1 2 1 0
$successLines = New-OuterLines $false
$successLines.AddRange([string[]](New-GcInitRecordLines $successRecord))
$success = Invoke-Trace 'a-success' $successLines
Assert-True ($success.gcInitializationInternal.outcome -eq 'gc_heap_initialize_succeeded') 'A: complete internal initialization success is recognized'

$semiFailureRecord = New-Record 3 $eFail $eFail 3 0 0 ([uint32]::MaxValue) 0 ([uint32]::MaxValue) 1
$semiFailureLines = New-OuterLines $true
$semiFailureLines.AddRange([string[]](New-GcInitRecordLines $semiFailureRecord))
$semiFailure = Invoke-Trace 'b-init-semi-shared-failure' $semiFailureLines
Assert-True ($semiFailure.gcInitializationInternal.outcome -eq 'init_semi_shared_failed') 'B: init_semi_shared false and source E_FAIL mapping are recognized'

$waitFailureRecord = New-Record 3 $eFail 0 2 1 2 1 3 0 3
$waitFailureLines = New-OuterLines $true
$waitFailureLines.AddRange([string[]](New-GcInitRecordLines $waitFailureRecord))
$waitFailure = Invoke-Trace 'c-wait-event-create-failure' $waitFailureLines
Assert-True ($waitFailure.gcInitializationInternal.outcome -eq 'wait_for_gc_event_create_failed') 'C: WaitForGCEvent creation failure is recognized after semi-shared success'

$incompleteRecord = New-Record 1 ([uint32]::MaxValue) ([uint32]::MaxValue) 4 ([uint32]::MaxValue) 0 ([uint32]::MaxValue) 0 ([uint32]::MaxValue) 0
$incompleteLines = New-OuterLines $true
$incompleteLines.AddRange([string[]](New-GcInitRecordLines $incompleteRecord))
$incomplete = Invoke-Trace 'd-missing-completion' $incompleteLines
Assert-True ($incomplete.gcInitializationInternal.outcome -eq 'internal_operation_incomplete') 'D: entered operation without completion stays incomplete and does not infer failure'

$missingLines = New-OuterLines $true
$missing = Invoke-Trace 'e-missing-internal-trace' $missingLines
Assert-True ($missing.gcInitializationInternal.outcome -eq 'unresolved_internally') 'E: outer failure without the entire internal trace stays unresolved'

$conflictRecord = New-Record 2 0 0 2 1 2 1 2 0 0
$conflictLines = New-OuterLines $false
$conflictLines.AddRange([string[]](New-GcInitRecordLines $conflictRecord))
$conflict = Invoke-Trace 'f-conflicting-evidence' $conflictLines
Assert-True ($conflict.gcInitializationInternal.outcome -eq 'malformed_or_conflicting') 'F: contradictory event state and Boolean result are rejected'

$r3HashBefore = Hash-File $r3Serial
$r3AnalysisPath = Join-Path $testRoot 'g-r3-guest-fixture.analysis.json'
& pwsh -NoProfile -ExecutionPolicy Bypass -File $analyzer -SerialPath $r3Serial -OutputPath $r3AnalysisPath | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Analyzer failed on the retained R3 guest fixture.' }
$r3 = Get-Content -LiteralPath $r3AnalysisPath -Raw | ConvertFrom-Json
$r3HashAfter = Hash-File $r3Serial
Assert-True ($r3.gcInitializationInternal.outcome -eq 'unresolved_internally') 'G: retained R3 guest fixture keeps the outer E_FAIL unresolved internally'
Assert-True ($r3HashBefore -eq $r3HashAfter) 'G: raw R3 serial remains byte-identical after analysis'

$allocationFailureRecord = New-Record 3 $eOutOfMemory 0 2 1 3 0 0 ([uint32]::MaxValue) 2
$allocationFailureLines = New-OuterLines $true
$allocationFailureLines.AddRange([string[]](New-GcInitRecordLines $allocationFailureRecord))
$allocationFailure = Invoke-Trace 'h-source-result-formats' $allocationFailureLines
Assert-True ($allocationFailure.gcInitializationInternal.outcome -eq 'wait_for_gc_event_allocation_failed') 'H: source integer results and hexadecimal HRESULT are interpreted correctly'

$manifest = [ordered]@{
    result='PASS'
    testRoot=$testRoot
    recordImplementationTest='PASS'
    recordTestExecutable=$recordExe
    recordTestSha256=Hash-File $recordExe
    parserScenarios=@(
        'A successful heap initialization'
        'B init_semi_shared failure'
        'C WaitForGCEvent creation failure'
        'D missing completion marker'
        'E missing entire internal trace'
        'F conflicting or malformed evidence'
        'G retained R3 guest fixture and raw serial preservation'
        'H source-defined result and hexadecimal HRESULT compatibility'
    )
    r3RawSerialSha256Before=$r3HashBefore
    r3RawSerialSha256After=$r3HashAfter
    noQemuLaunched=$true
    noQacReserved=$true
}
$manifestPath = Join-Path $testRoot 'hosted-test-manifest.json'
[System.IO.File]::WriteAllText($manifestPath,($manifest | ConvertTo-Json -Depth 6),[System.Text.UTF8Encoding]::new($false))
Write-Output "C188R12R4R5R4R5 hosted GC-init probe tests: PASS ($testRoot)"
