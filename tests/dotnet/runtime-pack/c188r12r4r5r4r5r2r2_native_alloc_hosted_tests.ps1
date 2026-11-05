param(
    [Parameter(Mandatory=$true)][string]$RepoRoot,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$DiagnosticMap,
    [Parameter(Mandatory=$true)][string]$ProductionRuntimeManifest,
    [string]$StartupImportsObject = '',
    [string]$PalContractObject = '',
    [string]$OriginalR2R1Serial = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [System.IO.Path]::GetFullPath($RepoRoot)
$output = [System.IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw "Refusing to overwrite hosted native-allocation test output: $output" }
New-Item -ItemType Directory -Force -Path $output | Out-Null
$testSource = Join-Path $root 'tests\dotnet\runtime-pack\c188r12r4r5r4r5r2r2_native_alloc_record_test.cpp'
$recordSource = Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_native_alloc_record.cpp'
$includeRoot = Join-Path $root 'tools\dotnet\runtime-pack\src\platform'
$exe = Join-Path $output 'native-alloc-record-test.exe'
$compileLog = Join-Path $output 'compile.log'
$vcvarsCandidates = @(
    'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat'
)
$vcvars = $vcvarsCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($vcvars)) { throw 'Visual C++ vcvars64.bat is required for the native allocation fixed-record test.' }
$compileBatch = Join-Path $output 'compile.bat'
$compileLines = @(
    '@echo off', 'setlocal', ('call "{0}" >nul' -f $vcvars), 'if errorlevel 1 exit /b %errorlevel%',
    ('cl.exe /nologo /std:c++17 /EHs-c- /DGUIDEXOS_NATIVEAOT_C188R12R4R5R4R5R2R2_NATIVE_ALLOC /I"{0}" "{1}" "{2}" /Fe:"{3}"' -f $includeRoot,$testSource,$recordSource,$exe),
    'if errorlevel 1 exit /b %errorlevel%', 'exit /b 0'
)
Set-Content -LiteralPath $compileBatch -Value $compileLines -Encoding ASCII
Push-Location -LiteralPath $output
try { & cmd.exe /d /c $compileBatch *> $compileLog } finally { Pop-Location }
if ($LASTEXITCODE -ne 0) { throw "Native allocation fixed-record test compile failed. See $compileLog" }
& $exe *> (Join-Path $output 'record-test.log')
if ($LASTEXITCODE -ne 0) { throw "Native allocation fixed-record test failed with $LASTEXITCODE." }

$backendSummarySource = Join-Path $root 'tests\dotnet\runtime-pack\c188r12r4r5r4r5r2r2_native_alloc_backend_summary_test.cpp'
$backendSummaryExe = Join-Path $output 'native-alloc-backend-summary-test.exe'
$backendSummaryCompileLog = Join-Path $output 'backend-summary-compile.log'
$backendSummaryCompileBatch = Join-Path $output 'backend-summary-compile.bat'
$backendSummaryLog = Join-Path $output 'backend-summary-test.log'
$backendSummaryCompileLines = @(
    '@echo off', 'setlocal', ('call "{0}" >nul' -f $vcvars), 'if errorlevel 1 exit /b %errorlevel%',
    ('cl.exe /nologo /std:c++17 /EHs-c- /I"{0}" /I"{1}" /I"{2}" "{3}" /Fe:"{4}"' -f $includeRoot,$root,(Join-Path $root 'kernel\include'),$backendSummarySource,$backendSummaryExe),
    'if errorlevel 1 exit /b %errorlevel%', 'exit /b 0'
)
Set-Content -LiteralPath $backendSummaryCompileBatch -Value $backendSummaryCompileLines -Encoding ASCII
Push-Location -LiteralPath $output
try { & cmd.exe /d /c $backendSummaryCompileBatch *> $backendSummaryCompileLog } finally { Pop-Location }
if ($LASTEXITCODE -ne 0) { throw "Native allocation backend summary test compile failed. See $backendSummaryCompileLog" }
& $backendSummaryExe *> $backendSummaryLog
if ($LASTEXITCODE -ne 0) { throw "Native allocation backend summary test failed with $LASTEXITCODE." }

$slotTestSource = Join-Path $root 'tests\dotnet\runtime-pack\c188r12r4r5r4r5r2r2_gc_vm_slot_table_test.cpp'
$slotTestPath = Join-Path $output 'gc-vm-slot-table-test.exe'
$slotTestCompileBatch = Join-Path $output 'gc-vm-slot-table-test-compile.bat'
$slotTestCompileLog = Join-Path $output 'gc-vm-slot-table-test-compile.log'
$slotTestLog = Join-Path $output 'gc-vm-slot-table-test.log'
$slotTestCompileLines = @(
    '@echo off', 'setlocal', ('call "{0}" >nul' -f $vcvars), 'if errorlevel 1 exit /b %errorlevel%',
    ('cl.exe /nologo /std:c++14 /EHs-c- /I"{0}" "{1}" /Fe:"{2}"' -f $root,$slotTestSource,$slotTestPath),
    'if errorlevel 1 exit /b %errorlevel%', 'exit /b 0'
)
Set-Content -LiteralPath $slotTestCompileBatch -Value $slotTestCompileLines -Encoding ASCII
Push-Location -LiteralPath $output
try { & cmd.exe /d /c $slotTestCompileBatch *> $slotTestCompileLog } finally { Pop-Location }
if ($LASTEXITCODE -ne 0) { throw "GC VM slot table hosted regression compile failed. See $slotTestCompileLog" }
& $slotTestPath *> $slotTestLog
if ($LASTEXITCODE -ne 0) { throw "GC VM slot table hosted regression failed with $LASTEXITCODE." }
$slotTestOutput = Get-Content -LiteralPath $slotTestLog -Raw
if ($slotTestOutput -notmatch 'legacy capacity=16, corrected capacity=256, assertions=2425, reserve/release cycles=1024') {
    throw 'GC VM slot table hosted regression did not report the 16-to-256 capacity boundary, 2,425 assertions, and 1,024 reuse cycles.'
}

$integrationTestPath = $null
$integrationTestHash = $null
if (-not [string]::IsNullOrWhiteSpace($StartupImportsObject) -or
    -not [string]::IsNullOrWhiteSpace($PalContractObject)) {
    if ([string]::IsNullOrWhiteSpace($StartupImportsObject) -or
        [string]::IsNullOrWhiteSpace($PalContractObject)) {
        throw 'Both diagnostic startup-import and PAL-contract objects are required for the actual operator-new integration test.'
    }
    foreach ($object in @($StartupImportsObject,$PalContractObject)) {
        if (-not (Test-Path -LiteralPath $object -PathType Leaf)) { throw "Native allocation integration object not found: $object" }
    }
    $integrationTestPath = Join-Path $output 'native-alloc-operator-new-test.exe'
    $integrationLog = Join-Path $output 'operator-new-integration-compile.log'
    $integrationBatch = Join-Path $output 'operator-new-integration-compile.bat'
    $integrationLines = @(
        '@echo off', 'setlocal', ('call "{0}" >nul' -f $vcvars), 'if errorlevel 1 exit /b %errorlevel%',
        ('cl.exe /nologo /std:c++17 /MT /GS- /GR- /EHs-c- /DGUIDEXOS_NATIVEAOT_C188R12R4R5R4R5R2R2_NATIVE_ALLOC /I"{0}" "{1}" "{2}" "{3}" "{4}" /Fe:"{5}" /link /FORCE:MULTIPLE /ENTRY:main' -f $includeRoot,(Join-Path $root 'tests\dotnet\runtime-pack\c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.cpp'),$recordSource,$StartupImportsObject,$PalContractObject,$integrationTestPath),
        'if errorlevel 1 exit /b %errorlevel%', 'exit /b 0'
    )
    Set-Content -LiteralPath $integrationBatch -Value $integrationLines -Encoding ASCII
    Push-Location -LiteralPath $output
    try { & cmd.exe /d /c $integrationBatch *> $integrationLog } finally { Pop-Location }
    if ($LASTEXITCODE -ne 0) { throw "Native allocation actual operator-new integration test compile failed. See $integrationLog" }
    & $integrationTestPath *> (Join-Path $output 'operator-new-integration-test.log')
    if ($LASTEXITCODE -ne 0) { throw "Native allocation actual operator-new integration test failed with $LASTEXITCODE." }
    $integrationTestHash = (Get-FileHash -LiteralPath $integrationTestPath -Algorithm SHA256).Hash.ToUpperInvariant()
}

$map = Get-Content -LiteralPath $DiagnosticMap -Raw
if ($map -notmatch '(?m)^\s+[0-9A-F]{4}:[0-9A-F]{8}\s+\?\?2@YAPEAX_KAEBUnothrow_t@std@@@Z\s+[0-9A-F]{16}\s+f\s+guidexos_nativeaot_startup_imports\.production\.obj\r?$' -or
    -not $map.Contains('guidexos_nativeaot_pal_virtual_alloc') -or
    -not $map.Contains('GuideXosNativeAotNativeAllocGetAttemptField')) {
    throw 'Diagnostic linked map does not retain the selected nothrow overload, PAL allocator, and fixed-record accessors.'
}
$productionManifest = Get-Content -LiteralPath $ProductionRuntimeManifest -Raw | ConvertFrom-Json
if ([bool]$productionManifest.c188r12r4r5r4r5r2r2NativeAllocationAttribution -or
    [bool]$productionManifest.c188r12r4r5r4r5r2SemiSharedAttribution -or
    [bool]$productionManifest.c188r12r4r5r4r5GcInitProbes -or
    [bool]$productionManifest.c188r12r4r5r4ThreadStoreReadiness -or
    [bool]$productionManifest.c188r12r4r5r3InitReturnContract -or
    [bool]$productionManifest.c188r12r4r5r1InitDiagnostics) {
    throw 'Proof-disabled production runtime-pack manifest contains diagnostic attribution profiles.'
}

if ([string]::IsNullOrWhiteSpace($OriginalR2R1Serial)) {
    $OriginalR2R1Serial = Join-Path $root 'out\dotnet\c188r12r4r5r4r5r2-semishared-attribution\qac-guest-r4r5r2r1-001\serial.log'
}
$originalHash = (Get-FileHash -LiteralPath $OriginalR2R1Serial -Algorithm SHA256).Hash.ToUpperInvariant()
if ($originalHash -cne '39402A5495C0DF35492C403B776EC9214A26D64D31B2D1CA558F196124905C5B') {
    throw 'The retained R2R1 serial hash changed.'
}

$analyzer = Join-Path $root 'scripts\dotnet\analyze-c188r12r4r5r4r5r2r2-native-alloc.ps1'
$guestRunner = Join-Path $root 'scripts\dotnet\run-c188r12r4r5-gc-ownership-qac-guest.ps1'
if (-not (Test-Path -LiteralPath $guestRunner -PathType Leaf)) { throw "Native allocation QAC runner is missing: $guestRunner" }
$guestRunnerText = Get-Content -LiteralPath $guestRunner -Raw
if (($guestRunnerText -notmatch '\$isNativeAllocationFailureAttribution\s*=\s*\[string\]\$candidateManifest\.mode\s+-ceq\s+''NativeAllocationFailureAttribution''' -and
     $guestRunnerText -notmatch '\$isNativeAllocationFailureAttribution\s*=\s*\[string\]\$candidateManifest\.mode\s+-in\s+@\(''NativeAllocationFailureAttribution'',''NativeAotSafeHandoff''\)') -or
    -not $guestRunnerText.Contains("[ValidateSet('R2R2R1','R2R2R2','R2R2R3','R2R2R4')][string]`$NativeAllocationGuestAttempt = 'R2R2R1'") -or
    -not $guestRunnerText.Contains('qac-guest-r2r2r2-001') -or
    -not $guestRunnerText.Contains('qac-guest-r2r2r3-001') -or
    -not $guestRunnerText.Contains('qac-guest-r2r2r4-001') -or
    -not $guestRunnerText.Contains('c188r12r4r5r4r5r2r2r4-pal-capacity-fix') -or
    -not $guestRunnerText.Contains('(?:rhResult|adapterStatus)') -or
    -not $guestRunnerText.Contains('guest-launch-attempt-C188R12R4R5R4R5$NativeAllocationGuestAttempt.json') -or
    -not $guestRunnerText.Contains('nativeAllocationGuestAttempt=if($isNativeAllocationFailureAttribution){$NativeAllocationGuestAttempt}else{$null}') -or
    -not $guestRunnerText.Contains('[System.IO.FileMode]::CreateNew') -or
    -not $guestRunnerText.Contains('candidate-integrity inventory') -or
    -not $guestRunnerText.Contains('verifiedArtifactHashCount') -or
    -not $guestRunnerText.Contains('palVmTrackingCapacity') -or
    -not $guestRunnerText.Contains('capacityCorrectionSymbolSizeHex') -or
    -not $guestRunnerText.Contains('capacityHelperSourceSha256') -or
    -not $guestRunnerText.Contains('guestBootLimit=1;automaticRetry=$false') -or
    -not $guestRunnerText.Contains('Test-NativeAllocationTraceComplete') -or
    -not $guestRunnerText.Contains('managed_main_gate=blocked') -or
    -not $guestRunnerText.Contains('native-allocation-diagnostic-record.raw.txt') -or
    -not $guestRunnerText.Contains('serialSha256BeforeAnalysis') -or
    -not $guestRunnerText.Contains('serialSha256AfterAnalysis') -or
    -not $guestRunnerText.Contains('qac-guest-ownership-identity.json') -or
    -not $guestRunnerText.Contains('-EvidenceKind guest_serial') -or
    -not $guestRunnerText.Contains('guest-manifest.json') -or
    -not $guestRunnerText.Contains('Set-Content -LiteralPath $manifestPath') -or
    -not $guestRunnerText.Contains('serialLog=$serial;serialLogSha256=') -or
    -not $guestRunnerText.Contains("[void](Invoke-Qmp 'quit')")) {
    throw 'Native allocation guest runner lacks its candidate gate, one-boot guard, complete trace, immutable raw serial, analyzer, ownership, or owned-shutdown contract.'
}
$gcStartupPalSource = Get-Content -LiteralPath (Join-Path $root 'kernel\core\nativeaot_pal_qemu_test.cpp') -Raw
$gcSlotTableSource = Get-Content -LiteralPath (Join-Path $root 'kernel\core\nativeaot_gc_vm_slots.h') -Raw
$productionPalSource = Get-Content -LiteralPath (Join-Path $root 'kernel\core\nativeaot_application.cpp') -Raw
if (-not $gcSlotTableSource.Contains('constexpr uint32_t kMaxGcVmSlots = 256u;') -or
    -not $gcStartupPalSource.Contains('GcVmSlotTable<') -or
    -not $gcStartupPalSource.Contains('g_gcVmSlots.findContaining(address)') -or
    -not $productionPalSource.Contains('constexpr uint32_t kMaxVmSlots = 256u;')) {
    throw 'The GC-startup PAL must retain the production NativeAOT PAL VM reservation-slot capacity.'
}
$reserveStart = $gcStartupPalSource.IndexOf('void* GUIDEXOS_NATIVEAOT_PAL_CALL startupReserve(')
$reserveEnd = $gcStartupPalSource.IndexOf('int32_t GUIDEXOS_NATIVEAOT_PAL_CALL startupCommit(')
$reserveFunction = $gcStartupPalSource.Substring($reserveStart, $reserveEnd - $reserveStart)
$reserveFailureCheck = $reserveFunction.IndexOf('if (result != gxos::runtime::virtual_memory::VmResult::Ok)')
if ($reserveStart -lt 0 -or $reserveEnd -le $reserveStart -or $reserveFailureCheck -lt 0 -or
    $reserveFailureCheck -gt $reserveFunction.IndexOf('slot.active = true;')) {
    throw 'The startup PAL must register a slot only after the real VM reserve succeeds.'
}
$releaseStart = $gcStartupPalSource.IndexOf('int32_t GUIDEXOS_NATIVEAOT_PAL_CALL startupRelease(')
$releaseEnd = $gcStartupPalSource.IndexOf('int32_t GUIDEXOS_NATIVEAOT_PAL_CALL startupReset(')
$releaseFunction = $gcStartupPalSource.Substring($releaseStart, $releaseEnd - $releaseStart)
$releaseFailureCheck = $releaseFunction.IndexOf('if (result != gxos::runtime::virtual_memory::VmResult::Ok) return -1;')
if ($releaseStart -lt 0 -or $releaseEnd -le $releaseStart -or $releaseFailureCheck -lt 0 -or
    $releaseFunction.IndexOf('slot->active = false;') -lt $releaseFailureCheck) {
    throw 'The startup PAL must retain tracking until the real VM release succeeds.'
}
$runnerGuestGate = $guestRunnerText.IndexOf('Test-NativeAllocationTraceComplete $serialText')
$runnerShutdown = $guestRunnerText.IndexOf("[void](Invoke-Qmp 'quit')")
if ($runnerGuestGate -lt 0 -or $runnerShutdown -lt $runnerGuestGate) {
    throw 'Native allocation runner must wait for its bounded trace before the owned QMP shutdown path.'
}
$runnerTokens = $null
$runnerParseErrors = $null
$runnerAst = [System.Management.Automation.Language.Parser]::ParseFile($guestRunner,[ref]$runnerTokens,[ref]$runnerParseErrors)
if ($runnerParseErrors.Count -ne 0) { throw 'Native allocation guest runner has PowerShell parse errors.' }
$outcomeFunctionAst = $runnerAst.Find({
    param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -ceq 'Get-NativeAllocationOutcome'
},$true)
if ($null -eq $outcomeFunctionAst) { throw 'Native allocation runner outcome classifier is missing.' }
. ([scriptblock]::Create($outcomeFunctionAst.Extent.Text))
$blockedWithoutSerial = Get-NativeAllocationOutcome -SourceRamdiskPreserved $true -PreflightPassed $false `
    -Admitted $false -ReleaseVerified $false -QacExitCode $null -SerialCaptured $false `
    -RawSerialPreserved $false -RecordComplete $false -AnalysisError $null -Classification $null
if ($blockedWithoutSerial -cne 'QAC_OR_EXECUTION_BLOCKED') {
    throw 'QAC preflight rejection without a guest serial must remain blocked, not be mislabeled as a regression.'
}
$preservationRegression = Get-NativeAllocationOutcome -SourceRamdiskPreserved $true -PreflightPassed $true `
    -Admitted $true -ReleaseVerified $false -QacExitCode 5 -SerialCaptured $true `
    -RawSerialPreserved $false -RecordComplete $false -AnalysisError $null -Classification $null
if ($preservationRegression -cne 'REGRESSION_DETECTED') {
    throw 'A captured raw serial changed during analysis and must remain a regression.'
}
$guestSuccessOutcome = Get-NativeAllocationOutcome -SourceRamdiskPreserved $true -PreflightPassed $true `
    -Admitted $true -ReleaseVerified $true -QacExitCode 0 -SerialCaptured $true `
    -RawSerialPreserved $true -RecordComplete $true -AnalysisError $null -Classification 'SUCCESS'
if ($guestSuccessOutcome -cne 'NATIVE_ALLOCATION_GUEST_ACCEPTED') {
    throw 'A complete successful native allocation must be accepted without implying GC success.'
}
$guestFailureSiteOutcome = Get-NativeAllocationOutcome -SourceRamdiskPreserved $true -PreflightPassed $true `
    -Admitted $true -ReleaseVerified $true -QacExitCode 0 -SerialCaptured $true `
    -RawSerialPreserved $true -RecordComplete $true -AnalysisError $null -Classification 'VM_COMMIT_BRANCH_0x0413'
if ($guestFailureSiteOutcome -cne 'NATIVE_ALLOC_FAILURE_SITE_PROVEN') {
    throw 'A complete source-backed backend failure site must remain distinct from a proven root cause.'
}
$unexpectedClassificationOutcome = Get-NativeAllocationOutcome -SourceRamdiskPreserved $true -PreflightPassed $true `
    -Admitted $true -ReleaseVerified $true -QacExitCode 0 -SerialCaptured $true `
    -RawSerialPreserved $true -RecordComplete $true -AnalysisError $null -Classification 'UNEXPECTED_CLASS'
if ($unexpectedClassificationOutcome -cne 'DIAGNOSTIC_INCONCLUSIVE') {
    throw 'An unrecognized analyzer classification must fail closed as inconclusive.'
}
$fixtureCounter = 0
function New-Fixture([string]$Kind) {
    $script:fixtureCounter++
    $inv = '00000001'
    $lines = [System.Collections.Generic.List[string]]::new()
    $runtimeCount = if ($Kind -eq 'kernel_slot_capacity') { 3 } else { 1 }
    $runtimeFailureOperation = [uint32]::MaxValue
    $runtimeFailureBranch = [uint32]::MaxValue
    $runtimeFailureStatus = [uint32]::MaxValue
    $runtimeFailureSource = 0u
    $palState = 2u
    $callbackEntered = 1u
    $runtimePointer = 1u
    $constructorEntered = 1u
    $backendCount = if ($Kind -eq 'kernel_slot_capacity') { 7u } else { 1u }
    $backendAttemptCount = [uint32]$backendCount
    $backendCompletedCount = [uint32]$backendCount
    $backendSuccessCount = [uint32]$backendCount
    $backendFailureCount = 0u
    $backendOverflow = 0u
    $backendFirstDropped = [uint32]::MaxValue
    $backendReserveCount = [uint32]$backendAttemptCount
    $backendCommitCount = [uint32]$backendAttemptCount
    $backendRollbackCount = 0u
    $backendTerminal = 1u
    $backendAggregateComplete = 1u
    $backendDistinctFailureCodes = @()
    $backendSchema = 'v2'
    $backendOperation = [uint32]::MaxValue
    $backendBranch = [uint32]::MaxValue
    $backendStatus = [uint32]::MaxValue
    $backendStatusSource = 0u
    $reserveResult = 0u
    $commitResult = 0u
    $releaseResult = [uint32]::MaxValue
    if ($Kind -eq 'pal_not_ready') {
        $palState=1u; $callbackEntered=0u; $runtimePointer=0u; $constructorEntered=0u
        $backendCount=0u; $backendAttemptCount=0u; $backendCompletedCount=0u
        $backendSuccessCount=0u; $backendReserveCount=0u; $backendCommitCount=0u
        $runtimeFailureOperation=0x0203u; $runtimeFailureBranch=0x0303u
    } elseif ($Kind -eq 'vm_commit_frame') {
        $runtimePointer=0u; $constructorEntered=0u; $runtimeFailureOperation=0x0203u
        $runtimeFailureBranch=0x0304u; $backendOperation=0x0206u; $backendBranch=0x0413u
        $backendStatus=7u; $backendStatusSource=1u; $commitResult=7u; $releaseResult=0u
        $backendSuccessCount=0u; $backendFailureCount=1u; $backendTerminal=2u
        $backendDistinctFailureCodes=@([pscustomobject]@{ operation=0x0206u; branch=0x0413u; status=7u; statusSource=1u })
    } elseif ($Kind -eq 'vm_reserve_address') {
        $runtimePointer=0u; $constructorEntered=0u; $runtimeFailureOperation=0x0203u
        $runtimeFailureBranch=0x0304u; $backendOperation=0x0205u; $backendBranch=0x0408u
        $backendStatus=6u; $backendStatusSource=1u; $reserveResult=6u; $commitResult=[uint32]::MaxValue
        $backendSuccessCount=0u; $backendFailureCount=1u; $backendTerminal=2u
        $backendCommitCount=0u
        $backendDistinctFailureCodes=@([pscustomobject]@{ operation=0x0205u; branch=0x0408u; status=6u; statusSource=1u })
    } elseif ($Kind -eq 'kernel_slot_capacity') {
        $runtimePointer=0u; $constructorEntered=0u; $runtimeFailureOperation=0x0203u
        $runtimeFailureBranch=0x0304u; $backendOperation=0x0204u; $backendBranch=0x0310u
        $backendStatus=0u; $backendStatusSource=2u; $reserveResult=[uint32]::MaxValue
        $commitResult=[uint32]::MaxValue; $releaseResult=[uint32]::MaxValue
        $backendSuccessCount=6u; $backendFailureCount=1u; $backendTerminal=2u
        $backendDistinctFailureCodes=@([pscustomobject]@{ operation=0x0204u; branch=0x0310u; status=0u; statusSource=2u })
    } elseif ($Kind -eq 'high_volume_success') {
        $backendCount=9u; $backendAttemptCount=2048u; $backendCompletedCount=2048u
        $backendSuccessCount=2048u; $backendOverflow=2039u; $backendFirstDropped=9u
        $backendReserveCount=2048u; $backendCommitCount=2048u
    } elseif ($Kind -eq 'failure_after_overflow') {
        $runtimePointer=0u; $constructorEntered=0u; $runtimeFailureOperation=0x0203u
        $runtimeFailureBranch=0x0304u; $backendOperation=0x0206u; $backendBranch=0x0413u
        $backendStatus=7u; $backendStatusSource=1u; $commitResult=7u; $releaseResult=0u
        $backendCount=9u; $backendAttemptCount=11u; $backendCompletedCount=11u
        $backendSuccessCount=10u; $backendFailureCount=1u; $backendOverflow=2u
        $backendFirstDropped=9u; $backendReserveCount=11u; $backendCommitCount=11u
        $backendTerminal=2u
        $backendDistinctFailureCodes=@(
            [pscustomobject]@{ operation=0x0206u; branch=0x0413u; status=7u; statusSource=1u },
            [pscustomobject]@{ operation=0x0207u; branch=0x0313u; status=5u; statusSource=1u }
        )
    } elseif ($Kind -eq 'incomplete_summary') {
        $backendAggregateComplete=0u; $backendTerminal=3u
    } elseif ($Kind -eq 'legacy_overflow') {
        $backendSchema='legacy'; $backendCount=9u; $backendOverflow=1u
    }
    $runtimeFirstFailed = if ($runtimeFailureOperation -eq [uint32]::MaxValue) { [uint32]::MaxValue } elseif ($Kind -eq 'kernel_slot_capacity') { 2u } else { 0u }
    $summary = @(0x4E413232u,1u,$runtimeCount,0u,$runtimeFirstFailed,
        $runtimeFailureOperation,$runtimeFailureBranch,$runtimeFailureStatus,$runtimeFailureSource)
    for ($field=0; $field -lt $summary.Count; $field++) {
        $lines.Add(('[C188R12R4R5R4R5R2R2_NATIVE_ALLOC] source=runtime invocation={0} sequence=FFFFFFFF field={1:X8} value={2:X8} auxiliary=00000000' -f $inv,$field,$summary[$field]))
    }
    for ($attemptIndex=0; $attemptIndex -lt $runtimeCount; $attemptIndex++) {
        $attempt = [uint32[]]::new(25)
        for ($field=0; $field -lt $attempt.Length; $field++) { $attempt[$field]=[uint32]::MaxValue }
        $attempt[0]=[uint32]$attemptIndex; $attempt[1]=0x0200u; $attempt[2]=2u; $attempt[3]=8u; $attempt[4]=0u
        $attempt[5]=8u; $attempt[6]=0u; $attempt[7]=0x0201u; $attempt[8]=0x12345678u; $attempt[9]=0u
        $attempt[10]=0x0202u; $attempt[11]=8u; $attempt[12]=0u; $attempt[13]=24u; $attempt[14]=0u
        $attempt[15]=$palState; $attempt[16]=$callbackEntered; $attempt[17]=0x10000u; $attempt[18]=0u
        $isFailureAttempt = ($attemptIndex -eq ($runtimeCount - 1) -and $runtimeFailureOperation -ne [uint32]::MaxValue)
        if ($isFailureAttempt) {
            $attempt[17]=0u; $attempt[19]=0u; $attempt[20]=0u
            $attempt[21]=$runtimeFailureOperation; $attempt[22]=$runtimeFailureBranch
            $attempt[23]=$runtimeFailureStatus; $attempt[24]=$runtimeFailureSource
        } else {
            $attempt[19]=1u; $attempt[20]=1u
        }
        for ($field=0; $field -lt $attempt.Length; $field++) {
            $lines.Add(('[C188R12R4R5R4R5R2R2_NATIVE_ALLOC] source=runtime invocation={0} sequence={1:X8} field={2:X8} value={3:X8} auxiliary=00000000' -f $inv,$attemptIndex,$field,$attempt[$field]))
        }
    }
    $backendFirstFailureIndex = [uint32]::MaxValue
    if ($backendFailureCount -gt 0u) {
        $backendFirstFailureIndex = if ($Kind -eq 'kernel_slot_capacity') { 6u } elseif ($Kind -eq 'failure_after_overflow') { 10u } else { 0u }
    }
    if ($backendSchema -eq 'v2') {
        $backendSummaryRows = @(
            [pscustomobject]@{ field=0u; value=$backendCount; auxiliary=$backendOverflow },
            [pscustomobject]@{ field=1u; value=$backendFirstFailureIndex; auxiliary=0u },
            [pscustomobject]@{ field=2u; value=$backendAttemptCount; auxiliary=$backendCompletedCount },
            [pscustomobject]@{ field=3u; value=$backendSuccessCount; auxiliary=$backendFailureCount },
            [pscustomobject]@{ field=4u; value=$backendReserveCount; auxiliary=$backendCommitCount },
            [pscustomobject]@{ field=5u; value=$backendRollbackCount; auxiliary=$backendFirstDropped },
            [pscustomobject]@{ field=6u; value=$backendTerminal; auxiliary=$backendAggregateComplete },
            [pscustomobject]@{ field=7u; value=$backendOperation; auxiliary=$backendBranch },
            [pscustomobject]@{ field=8u; value=$backendStatus; auxiliary=$backendStatusSource },
            [pscustomobject]@{ field=9u; value=$backendDistinctFailureCodes.Count; auxiliary=0u },
            [pscustomobject]@{ field=10u; value=0u; auxiliary=0u }
        )
        foreach ($row in $backendSummaryRows) {
            $field = [uint32](0x80000000u -bor [uint32]$row.field)
            $lines.Add(('[C188R12R4R5R4R5R2R2_NATIVE_ALLOC] source=backend invocation={0} sequence=FFFFFFFF field={1:X8} value={2:X8} auxiliary={3:X8}' -f $inv,$field,$row.value,$row.auxiliary))
        }
        for ($failureIndex=0; $failureIndex -lt $backendDistinctFailureCodes.Count; $failureIndex++) {
            $failureCode = $backendDistinctFailureCodes[$failureIndex]
            $field = [uint32](0x80000100u + (2u * [uint32]$failureIndex))
            $lines.Add(('[C188R12R4R5R4R5R2R2_NATIVE_ALLOC] source=backend invocation={0} sequence=FFFFFFFF field={1:X8} value={2:X8} auxiliary={3:X8}' -f $inv,$field,$failureCode.operation,$failureCode.branch))
            $lines.Add(('[C188R12R4R5R4R5R2R2_NATIVE_ALLOC] source=backend invocation={0} sequence=FFFFFFFF field={1:X8} value={2:X8} auxiliary={3:X8}' -f $inv,($field + 1u),$failureCode.status,$failureCode.statusSource))
        }
    } else {
        $lines.Add(('[C188R12R4R5R4R5R2R2_NATIVE_ALLOC] source=backend invocation={0} sequence=FFFFFFFF field=80000000 value={1:X8} auxiliary={2:X8}' -f $inv,$backendCount,$backendOverflow))
        $lines.Add(('[C188R12R4R5R4R5R2R2_NATIVE_ALLOC] source=backend invocation={0} sequence=FFFFFFFF field=80000001 value=FFFFFFFF auxiliary=00000000' -f $inv))
    }
    if ($Kind -ne 'pal_not_ready') {
        for ($backendIndex=0; $backendIndex -lt $backendCount; $backendIndex++) {
            $back = [uint32[]]::new(34)
            for ($field=0; $field -lt $back.Length; $field++) { $back[$field]=[uint32]::MaxValue }
            $back[0]=[uint32]$backendIndex; $back[1]=24u; $back[2]=0x3000u; $back[3]=0x1000u; $back[4]=16u
            $back[5]=[uint32][Math]::Min(15,$backendIndex); $back[6]=[uint32][Math]::Min(15,$backendIndex)
            $back[7]=1u; $back[8]=128u
            $thisBackendFailure = ($backendIndex -eq $backendFirstFailureIndex)
            if ($Kind -eq 'kernel_slot_capacity' -and $thisBackendFailure) {
                $back[5]=16u; $back[6]=[uint32]::MaxValue; $back[9]=[uint32]::MaxValue
                $back[10]=0u; $back[11]=[uint32]::MaxValue; $back[12]=[uint32]::MaxValue
                $back[13]=0u; $back[14]=$backendOperation; $back[15]=$backendBranch
                $back[16]=$backendStatus; $back[17]=$backendStatusSource
            } elseif ($backendIndex -lt $backendCount -and $Kind -eq 'kernel_slot_capacity') {
                $back[9]=0u; $back[10]=0x1000u; $back[11]=0u; $back[12]=[uint32]::MaxValue
                $back[13]=1u; $back[14]=[uint32]::MaxValue; $back[15]=[uint32]::MaxValue
                $back[16]=[uint32]::MaxValue; $back[17]=0u
            } elseif ($thisBackendFailure) {
                $back[9]=$reserveResult; $back[10]=if ($Kind -eq 'vm_reserve_address') { 0u } else { 0x1000u }
                $back[11]=$commitResult; $back[12]=$releaseResult; $back[13]=$runtimePointer
                $back[14]=$backendOperation; $back[15]=$backendBranch
                $back[16]=$backendStatus; $back[17]=$backendStatusSource
            } else {
                $back[9]=0u; $back[10]=0x1000u; $back[11]=0u; $back[12]=[uint32]::MaxValue
                $back[13]=1u; $back[14]=[uint32]::MaxValue; $back[15]=[uint32]::MaxValue
                $back[16]=[uint32]::MaxValue; $back[17]=0u
            }
            for ($field=0; $field -lt $back.Length; $field++) {
                $lines.Add(('[C188R12R4R5R4R5R2R2_NATIVE_ALLOC] source=backend invocation={0} sequence={1:X8} field={2:X8} value={3:X8} auxiliary=00000000' -f $inv,$backendIndex,$field,$back[$field]))
            }
        }
    }
    return ,$lines
}

function Assert-Classification([string]$Kind, [string]$Expected) {
    $fixturePath = Join-Path $output ("fixture-{0}.serial.log" -f $Kind)
    $lines = New-Fixture $Kind
    if ($Kind -eq 'missing') { $lines = [System.Collections.Generic.List[string]]::new() }
    if ($Kind -eq 'incomplete') { $lines = [System.Collections.Generic.List[string]]::new(); foreach ($line in (New-Fixture 'success')) { if ($line -notmatch 'source=runtime invocation=00000001 sequence=00000000 field=0000000D ') { $lines.Add($line) } } }
    [System.IO.File]::WriteAllLines($fixturePath,$lines,[System.Text.UTF8Encoding]::new($false))
    $reportPath = Join-Path $output ("fixture-{0}.json" -f $Kind)
    & $analyzer -SerialPath $fixturePath -OutputPath $reportPath -EvidenceKind hosted_fixture
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    if ([string]$report.results[0].classification -cne $Expected -or [bool]$report.guestRootCauseAccepted) {
        throw "Analyzer classified $Kind as '$($report.results[0].classification)' (expected '$Expected')."
    }
}

Assert-Classification 'success' 'SUCCESS'
Assert-Classification 'vm_commit_frame' 'VM_COMMIT_BRANCH_0x0413'
Assert-Classification 'vm_reserve_address' 'VM_RESERVE_BRANCH_0x0408'
Assert-Classification 'kernel_slot_capacity' 'KERNEL_PAL_SLOT_CAPACITY'
Assert-Classification 'pal_not_ready' 'PAL_NOT_READY'
Assert-Classification 'missing' 'INCOMPLETE_MISSING_RUNTIME_RECORD'
Assert-Classification 'incomplete' 'INCOMPLETE_DIAGNOSTIC_RECORD'
Assert-Classification 'incomplete_summary' 'INCOMPLETE_DIAGNOSTIC_RECORD'
Assert-Classification 'legacy_overflow' 'INCOMPLETE_DIAGNOSTIC_RECORD'

function Assert-BackendSummaryTamper(
    [string]$Kind, [string]$Field, [string]$Value,
    [string]$SecondField = $null, [string]$SecondValue = $null,
    [string]$AuxiliaryField = $null, [string]$AuxiliaryValue = $null) {
    $lines = New-Fixture $Kind
    $mutations = @([pscustomobject]@{ field=$Field; property='value'; value=$Value })
    if (-not [string]::IsNullOrWhiteSpace($SecondField)) {
        $mutations += [pscustomobject]@{ field=$SecondField; property='value'; value=$SecondValue }
    }
    if (-not [string]::IsNullOrWhiteSpace($AuxiliaryField)) {
        $mutations += [pscustomobject]@{ field=$AuxiliaryField; property='auxiliary'; value=$AuxiliaryValue }
    }
    foreach ($mutation in $mutations) {
        $needle = "source=backend invocation=00000001 sequence=FFFFFFFF field=$($mutation.field) "
        $found = $false
        for ($lineIndex=0; $lineIndex -lt $lines.Count; $lineIndex++) {
            if ($lines[$lineIndex].Contains($needle)) {
                $lines[$lineIndex] = [regex]::Replace(
                    $lines[$lineIndex], "$($mutation.property)=[0-9A-F]{8}",
                    "$($mutation.property)=$($mutation.value)")
                $found = $true
                break
            }
        }
        if (-not $found) { throw "Could not locate backend summary field $($mutation.field) in $Kind fixture." }
    }
    $safeName = ($Kind + '-' + $Field + '-' + $Value) -replace '[^A-Za-z0-9-]', '_'
    $fixturePath = Join-Path $output ("fixture-tampered-{0}.serial.log" -f $safeName)
    [System.IO.File]::WriteAllLines($fixturePath,$lines,[System.Text.UTF8Encoding]::new($false))
    $reportPath = Join-Path $output ("fixture-tampered-{0}.json" -f $safeName)
    & $analyzer -SerialPath $fixturePath -OutputPath $reportPath -EvidenceKind hosted_fixture
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    if ([string]$report.results[0].classification -cne 'INCOMPLETE_DIAGNOSTIC_RECORD' -or
        [bool]$report.results[0].backendSummary.aggregateComplete) {
        throw "Analyzer accepted inconsistent backend summary mutation in $Kind field $Field."
    }
}

Assert-BackendSummaryTamper 'success' '80000003' '00000002'
Assert-BackendSummaryTamper 'success' '80000004' '00000002'
Assert-BackendSummaryTamper 'success' '80000006' '00000003'
Assert-BackendSummaryTamper 'failure_after_overflow' '80000102' '00000206' '80000103' '00000007' '80000102' '00000413'

function Get-FixtureReport([string]$Kind) {
    $fixturePath = Join-Path $output ("fixture-{0}.serial.log" -f $Kind)
    $lines = New-Fixture $Kind
    [System.IO.File]::WriteAllLines($fixturePath,$lines,[System.Text.UTF8Encoding]::new($false))
    $reportPath = Join-Path $output ("fixture-{0}.json" -f $Kind)
    & $analyzer -SerialPath $fixturePath -OutputPath $reportPath -EvidenceKind hosted_fixture
    return Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
}
$highVolumeReport = Get-FixtureReport 'high_volume_success'
$highVolumeSummary = $highVolumeReport.results[0].backendSummary
if ($highVolumeReport.results[0].classification -cne 'SUCCESS' -or
    -not $highVolumeSummary.attemptedCountExact -or $highVolumeSummary.attemptedCount -ne 2048 -or
    $highVolumeSummary.detailedRows -ne 9 -or $highVolumeSummary.droppedDetailCount -ne 2039 -or
    $highVolumeSummary.firstDroppedAttempt -ne 9 -or -not $highVolumeSummary.aggregateComplete) {
    throw 'High-volume backend summary did not preserve exact attempt, detail-drop, and completion totals.'
}
$lateFailureReport = Get-FixtureReport 'failure_after_overflow'
$lateFailureSummary = $lateFailureReport.results[0].backendSummary
if ($lateFailureReport.results[0].classification -cne 'VM_COMMIT_BRANCH_0x0413' -or
    $lateFailureSummary.firstFailureAttempt -ne 10 -or $lateFailureSummary.attemptedCount -ne 11 -or
    $lateFailureSummary.distinctFailureCount -ne 2 -or $lateFailureSummary.distinctFailureCodes.Count -ne 2 -or
    $lateFailureSummary.distinctFailureCodes[1].operation -ne 0x0207 -or
    -not $lateFailureSummary.aggregateComplete) {
    throw 'Failure after detail overflow or distinct cleanup failure codes were not retained in the aggregate.'
}
$legacyOverflowReport = Get-FixtureReport 'legacy_overflow'
$legacySummary = $legacyOverflowReport.results[0].backendSummary
if ($legacySummary.attemptedCountExact -or $null -ne $legacySummary.attemptedCount -or
    $legacySummary.minimumAttemptCount -ne 10 -or $legacySummary.firstDroppedAttempt -ne 9 -or
    $legacySummary.legacyOverflowFlag -ne 1) {
    throw 'Legacy boolean overflow was incorrectly upgraded to an exact attempt count.'
}

$result = [ordered]@{
    result='PASS'
    evidenceKind='hosted fixed-record and shared PAL slot-table tests plus synthetic analyzer fixtures'
    qemuLaunched=$false
    qacReserved=$false
    nativeAllocRecordTest=$exe
    nativeAllocRecordTestSha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToUpperInvariant()
    nativeAllocBackendSummaryTest=$backendSummaryExe
    nativeAllocBackendSummaryTestSha256=(Get-FileHash -LiteralPath $backendSummaryExe -Algorithm SHA256).Hash.ToUpperInvariant()
    nativeAllocBackendSummaryCases=@(
        'successful startup';'bounded 2,048-attempt success summary';'failure before detail overflow'
        'first failure and distinct cleanup codes after detail overflow'
        'unfinished terminal classification';'counter saturation fails closed'
        '64-bit outcome total rejects wrapped 32-bit count sums'
        'analyzer rejects inconsistent outcome, operation, terminal, and duplicate failure summaries'
    )
    gcVmSlotTableTest=$slotTestPath
    gcVmSlotTableTestSha256=(Get-FileHash -LiteralPath $slotTestPath -Algorithm SHA256).Hash.ToUpperInvariant()
    gcVmSlotTableTestSemantics='shared PAL slot-table implementation with synthetic reserve/commit/release backend; no guest memory claimed'
    gcVmSlotTableLegacyCapacity=16
    gcVmSlotTableCapacity=256
    gcVmSlotTableAssertions=2425
    gcVmSlotTableReserveReleaseCycles=1024
    actualOperatorNewIntegrationTest=$integrationTestPath
    actualOperatorNewIntegrationTestSha256=$integrationTestHash
    actualOperatorNewFailureMode=if ($null -ne $integrationTestPath) { 'actual linked operator-new and PAL contract; callback refusal is synthetic' } else { 'not-run' }
    compileLogSha256=(Get-FileHash -LiteralPath $compileLog -Algorithm SHA256).Hash.ToUpperInvariant()
    originalR2R1Serial=$OriginalR2R1Serial
    originalR2R1SerialSha256=$originalHash
    nativeAllocationQacRunnerContract='PASS'
    nativeAllocationRunnerGuard='C188R12R4R5R4R5R2R2R4 CreateNew; guest boot limit 1; automatic retry disabled'
    nativeAllocationRawSerialPreservation='PASS; immutable serial hash bracket and separate diagnostic record files'
    nativeAllocationQacOwnershipAndShutdown='PASS; reservation owner, QEMU child identity, QMP-owned quit'
    nativeAllocationBlockedPreflightOutcome=$blockedWithoutSerial
    nativeAllocationRawSerialMutationOutcome=$preservationRegression
    nativeAllocationSuccessOutcome=$guestSuccessOutcome
    nativeAllocationFailureSiteOutcome=$guestFailureSiteOutcome
    nativeAllocationUnknownClassOutcome=$unexpectedClassificationOutcome
    analyzer=$analyzer
    fixtureClassifications=@('SUCCESS','VM_COMMIT_BRANCH_0x0413','VM_RESERVE_BRANCH_0x0408','KERNEL_PAL_SLOT_CAPACITY','PAL_NOT_READY','INCOMPLETE_MISSING_RUNTIME_RECORD','INCOMPLETE_DIAGNOSTIC_RECORD','INCOMPLETE_DIAGNOSTIC_RECORD(summary incomplete)','INCOMPLETE_DIAGNOSTIC_RECORD(legacy overflow)')
    highVolumeBackendAttemptCount=2048
    highVolumeBackendDetailRows=9
    highVolumeBackendDroppedDetailCount=2039
    lateBackendFailureAttempt=10
    lateBackendDistinctFailureCodeCount=2
    legacyOverflowExactAttemptCount=$false
    diagnosticMapOperatorNew='??2@YAPEAX_KAEBUnothrow_t@std@@@Z -> guidexos_nativeaot_startup_imports.production.obj'
    productionDiagnosticProfilesAbsent=$true
}
$result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $output 'hosted-test-results.json') -Encoding utf8
Write-Output "Native allocation hosted tests: PASS ($output)"
