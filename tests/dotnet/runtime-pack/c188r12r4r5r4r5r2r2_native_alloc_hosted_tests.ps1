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
& cmd.exe /d /c $compileBatch *> $compileLog
if ($LASTEXITCODE -ne 0) { throw "Native allocation fixed-record test compile failed. See $compileLog" }
& $exe *> (Join-Path $output 'record-test.log')
if ($LASTEXITCODE -ne 0) { throw "Native allocation fixed-record test failed with $LASTEXITCODE." }

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
    & cmd.exe /d /c $integrationBatch *> $integrationLog
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
if ($guestRunnerText -notmatch '\$isNativeAllocationFailureAttribution\s*=\s*\[string\]\$candidateManifest\.mode\s+-ceq\s+''NativeAllocationFailureAttribution''' -or
    -not $guestRunnerText.Contains("[ValidateSet('R2R2R1','R2R2R2')][string]`$NativeAllocationGuestAttempt = 'R2R2R1'") -or
    -not $guestRunnerText.Contains('qac-guest-r2r2r2-001') -or
    -not $guestRunnerText.Contains('guest-launch-attempt-C188R12R4R5R4R5$NativeAllocationGuestAttempt.json') -or
    -not $guestRunnerText.Contains('nativeAllocationGuestAttempt=if($isNativeAllocationFailureAttribution){$NativeAllocationGuestAttempt}else{$null}') -or
    -not $guestRunnerText.Contains('[System.IO.FileMode]::CreateNew') -or
    -not $guestRunnerText.Contains('candidate-integrity inventory') -or
    -not $guestRunnerText.Contains('verifiedArtifactHashCount') -or
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
    $runtimeCount = if ($Kind -eq 'pal_not_ready') { 1 } else { 1 }
    $runtimeFailureOperation = [uint32]::MaxValue
    $runtimeFailureBranch = [uint32]::MaxValue
    $runtimeFailureStatus = [uint32]::MaxValue
    $runtimeFailureSource = 0u
    $palState = 2u
    $callbackEntered = 1u
    $runtimePointer = 1u
    $constructorEntered = 1u
    $backendCount = 1u
    $backendOverflow = 0u
    $backendOperation = [uint32]::MaxValue
    $backendBranch = [uint32]::MaxValue
    $backendStatus = [uint32]::MaxValue
    $backendStatusSource = 0u
    $reserveResult = 0u
    $commitResult = 0u
    $releaseResult = [uint32]::MaxValue
    if ($Kind -eq 'pal_not_ready') {
        $palState=1u; $callbackEntered=0u; $runtimePointer=0u; $constructorEntered=0u
        $backendCount=0u; $runtimeFailureOperation=0x0203u; $runtimeFailureBranch=0x0303u
    } elseif ($Kind -eq 'vm_commit_frame') {
        $runtimePointer=0u; $constructorEntered=0u; $runtimeFailureOperation=0x0203u
        $runtimeFailureBranch=0x0304u; $backendOperation=0x0206u; $backendBranch=0x0413u
        $backendStatus=7u; $backendStatusSource=1u; $commitResult=7u; $releaseResult=0u
    } elseif ($Kind -eq 'vm_reserve_address') {
        $runtimePointer=0u; $constructorEntered=0u; $runtimeFailureOperation=0x0203u
        $runtimeFailureBranch=0x0304u; $backendOperation=0x0205u; $backendBranch=0x0408u
        $backendStatus=6u; $backendStatusSource=1u; $reserveResult=6u; $commitResult=[uint32]::MaxValue
    } elseif ($Kind -eq 'kernel_slot_capacity') {
        $runtimePointer=0u; $constructorEntered=0u; $runtimeFailureOperation=0x0203u
        $runtimeFailureBranch=0x0304u; $backendOperation=0x0204u; $backendBranch=0x0310u
        $backendStatus=0u; $backendStatusSource=2u; $reserveResult=[uint32]::MaxValue
        $commitResult=[uint32]::MaxValue; $releaseResult=[uint32]::MaxValue
    }
    $summary = @(0x4E413232u,1u,$runtimeCount,0u,($(if ($runtimeFailureOperation -eq [uint32]::MaxValue) { [uint32]::MaxValue } else { 0u })),
        $runtimeFailureOperation,$runtimeFailureBranch,$runtimeFailureStatus,$runtimeFailureSource)
    for ($field=0; $field -lt $summary.Count; $field++) {
        $lines.Add(('[C188R12R4R5R4R5R2R2_NATIVE_ALLOC] source=runtime invocation={0} sequence=FFFFFFFF field={1:X8} value={2:X8} auxiliary=00000000' -f $inv,(0x80000000u+$field),$summary[$field]))
    }
    $attempt = [uint32[]]::new(25)
    for ($field=0; $field -lt $attempt.Length; $field++) { $attempt[$field]=[uint32]::MaxValue }
    $attempt[0]=0u; $attempt[1]=0x0200u; $attempt[2]=2u; $attempt[3]=8u; $attempt[4]=0u
    $attempt[5]=8u; $attempt[6]=0u; $attempt[7]=0x0201u; $attempt[8]=0x12345678u; $attempt[9]=0u
    $attempt[10]=0x0202u; $attempt[11]=8u; $attempt[12]=0u; $attempt[13]=24u; $attempt[14]=0u
    $attempt[15]=$palState; $attempt[16]=$callbackEntered; $attempt[17]=0x10000u; $attempt[18]=0u
    $attempt[19]=$runtimePointer; $attempt[20]=$constructorEntered; $attempt[21]=$runtimeFailureOperation
    $attempt[22]=$runtimeFailureBranch; $attempt[23]=$runtimeFailureStatus; $attempt[24]=$runtimeFailureSource
    for ($field=0; $field -lt $attempt.Length; $field++) {
        $lines.Add(('[C188R12R4R5R4R5R2R2_NATIVE_ALLOC] source=runtime invocation={0} sequence=00000000 field={1:X8} value={2:X8} auxiliary=00000000' -f $inv,$field,$attempt[$field]))
    }
    if ($Kind -ne 'pal_not_ready') {
        $backendSummaryKey = 0x80000000u
        $lines.Add(('[C188R12R4R5R4R5R2R2_NATIVE_ALLOC] source=backend invocation={0} sequence=FFFFFFFF field={1:X8} value={2:X8} auxiliary={3:X8}' -f $inv,$backendSummaryKey,$backendCount,$backendOverflow))
        $firstFailed = if ($backendOperation -eq [uint32]::MaxValue) { [uint32]::MaxValue } else { 0u }
        $lines.Add(('[C188R12R4R5R4R5R2R2_NATIVE_ALLOC] source=backend invocation={0} sequence=FFFFFFFF field=80000001 value={1:X8} auxiliary=00000000' -f $inv,$firstFailed))
    } else {
        $lines.Add(('[C188R12R4R5R4R5R2R2_NATIVE_ALLOC] source=backend invocation={0} sequence=FFFFFFFF field=80000000 value=00000000 auxiliary=00000000' -f $inv))
        $lines.Add(('[C188R12R4R5R4R5R2R2_NATIVE_ALLOC] source=backend invocation={0} sequence=FFFFFFFF field=80000001 value=FFFFFFFF auxiliary=00000000' -f $inv))
    }
    if ($Kind -ne 'pal_not_ready') {
        $back = [uint32[]]::new(34)
        for ($field=0; $field -lt $back.Length; $field++) { $back[$field]=[uint32]::MaxValue }
        $back[0]=0u; $back[1]=24u; $back[2]=0x3000u; $back[3]=0x1000u; $back[4]=16u
        $back[5]=0u; $back[6]=0u; $back[7]=1u; $back[8]=128u; $back[9]=$reserveResult
        $back[10]=0x1000u; $back[11]=$commitResult; $back[12]=$releaseResult
        $back[13]=$runtimePointer; $back[14]=$backendOperation; $back[15]=$backendBranch
        $back[16]=$backendStatus; $back[17]=$backendStatusSource
        for ($field=0; $field -lt $back.Length; $field++) {
            $lines.Add(('[C188R12R4R5R4R5R2R2_NATIVE_ALLOC] source=backend invocation={0} sequence=00000000 field={1:X8} value={2:X8} auxiliary=00000000' -f $inv,$field,$back[$field]))
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

$result = [ordered]@{
    result='PASS'
    evidenceKind='hosted synthetic fixtures and fixed-record unit test'
    qemuLaunched=$false
    qacReserved=$false
    nativeAllocRecordTest=$exe
    nativeAllocRecordTestSha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToUpperInvariant()
    actualOperatorNewIntegrationTest=$integrationTestPath
    actualOperatorNewIntegrationTestSha256=$integrationTestHash
    actualOperatorNewFailureMode=if ($null -ne $integrationTestPath) { 'actual linked operator-new and PAL contract; callback refusal is synthetic' } else { 'not-run' }
    compileLogSha256=(Get-FileHash -LiteralPath $compileLog -Algorithm SHA256).Hash.ToUpperInvariant()
    originalR2R1Serial=$OriginalR2R1Serial
    originalR2R1SerialSha256=$originalHash
    nativeAllocationQacRunnerContract='PASS'
    nativeAllocationRunnerGuard='C188R12R4R5R4R5R2R2R1 CreateNew; guest boot limit 1; automatic retry disabled'
    nativeAllocationRawSerialPreservation='PASS; immutable serial hash bracket and separate diagnostic record files'
    nativeAllocationQacOwnershipAndShutdown='PASS; reservation owner, QEMU child identity, QMP-owned quit'
    nativeAllocationBlockedPreflightOutcome=$blockedWithoutSerial
    nativeAllocationRawSerialMutationOutcome=$preservationRegression
    nativeAllocationSuccessOutcome=$guestSuccessOutcome
    nativeAllocationFailureSiteOutcome=$guestFailureSiteOutcome
    nativeAllocationUnknownClassOutcome=$unexpectedClassificationOutcome
    analyzer=$analyzer
    fixtureClassifications=@('SUCCESS','VM_COMMIT_BRANCH_0x0413','VM_RESERVE_BRANCH_0x0408','KERNEL_PAL_SLOT_CAPACITY','PAL_NOT_READY','INCOMPLETE_MISSING_RUNTIME_RECORD','INCOMPLETE_DIAGNOSTIC_RECORD')
    diagnosticMapOperatorNew='??2@YAPEAX_KAEBUnothrow_t@std@@@Z -> guidexos_nativeaot_startup_imports.production.obj'
    productionDiagnosticProfilesAbsent=$true
}
$result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $output 'hosted-test-results.json') -Encoding utf8
Write-Output "Native allocation hosted tests: PASS ($output)"
