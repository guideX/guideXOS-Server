param([string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [System.IO.Path]::GetFullPath($RepoRoot)
$runId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$testRoot = Join-Path $root "out\dotnet\c188r12r4r5r1-runtime-init-attribution\hosted-$runId"
if (Test-Path -LiteralPath $testRoot) { throw "Refusing to overwrite hosted evidence: $testRoot" }
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
$analyzer = Join-Path $root 'scripts\dotnet\analyze-c188r12r4r5r1-init-serial.ps1'
$runner = Join-Path $root 'scripts\dotnet\run-c188r12r4r5-gc-ownership-qac-guest.ps1'
$kernelSource = Join-Path $root 'kernel\core\nativeaot_pal_qemu_test.cpp'
$runtimeSource = Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_platform.cpp'

function Assert-True([bool]$Value,[string]$Label) {
    if (-not $Value) { throw "C188R12R4R5R1 assertion failed: $Label" }
}
function Add-TraceEvent([System.Collections.Generic.List[string]]$Lines,[string]$Stage,[string]$Event,
                        [string]$Code='FFFFFFFF',[string]$Singleton='00000000') {
    $Lines.Add("[C188R12R4R5R1_INIT] invocation=00000001 stage=$Stage event=$Event code=$Code singleton=$Singleton")
}
function Add-Stage([System.Collections.Generic.List[string]]$Lines,[string]$Stage,[string]$Code='00000000',
                   [string]$Singleton='00000000') {
    Add-TraceEvent $Lines $Stage '00000001'
    Add-TraceEvent $Lines $Stage '00000002' $Code $Singleton
}
function Add-FailedStage([System.Collections.Generic.List[string]]$Lines,[string]$Stage,[string]$Code,
                         [string]$Singleton='00000000') {
    Add-TraceEvent $Lines $Stage '00000001'
    Add-TraceEvent $Lines $Stage '00000003' $Code $Singleton
}
function New-TraceText([System.Collections.Generic.List[string]]$Lines,[Nullable[uint32]]$RingOverflow=$null,
                       [uint32]$SerialDropped=0,[bool]$End=$true,[bool]$TerminalNewline=$true,
                       [string]$Gate='') {
    $eventCount = @($Lines | Where-Object { $_ -match '^\[C188R12R4R5R1_INIT\] invocation=' }).Count
    if ($End) {
        $overflowValue = if ($null -eq $RingOverflow) { 0 } else { $RingOverflow.Value }
        $Lines.Add(('[C188R12R4R5R1_INIT] trace_end invocation=00000001 count={0:X8} overflow={1:X8} serialDropped={2:X8} rhResult={3}' -f $eventCount,$overflowValue,$SerialDropped,($(if ($Gate -match 'rhResult=([0-9A-F]{8})') {$Matches[1]} else {'00000000'}))))
    }
    if ($Gate) { $Lines.Add($Gate) }
    $text = $Lines -join "`n"
    if ($TerminalNewline) { $text += "`n" }
    return $text
}
function Invoke-TraceAnalysis([string]$Name,[string]$Text,[switch]$TimedOut) {
    $serialPath = Join-Path $testRoot "$Name.serial.log"
    $analysisPath = Join-Path $testRoot "$Name.analysis.json"
    [System.IO.File]::WriteAllBytes($serialPath,[System.Text.Encoding]::UTF8.GetBytes($Text))
    $arguments = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$analyzer,
        '-SerialPath',$serialPath,'-OutputPath',$analysisPath)
    if ($TimedOut) { $arguments += '-GuestTimedOut' }
    & pwsh @arguments | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Trace analyzer failed for $Name." }
    return [pscustomobject]@{
        serial=$serialPath; analysisPath=$analysisPath
        analysis=(Get-Content -LiteralPath $analysisPath -Raw | ConvertFrom-Json)
    }
}

# A. Full, ordered path through the source-defined RhInitialize/GC boundaries.
$success = [System.Collections.Generic.List[string]]::new()
Add-Stage $success '00000001'
Add-TraceEvent $success '00000002' '00000001'
Add-TraceEvent $success '00000010' '00000001'
Add-Stage $success '00000011'
Add-Stage $success '00000012' '00000000' '00000010'
Add-Stage $success '00000013' '00000000' '00000010'
Add-TraceEvent $success '00000010' '00000002' '00000000' '00000010'
Add-Stage $success '00000025' '00000000' '00000010'
Add-TraceEvent $success '00000026' '00000004' '00000001' '00000010'
Add-TraceEvent $success '00000020' '00000001' 'FFFFFFFF' '00000013'
Add-Stage $success '00000021' '00000000' '00000013'
Add-Stage $success '00000022' '00000000' '00000013'
Add-Stage $success '00000023' '00000000' '00000013'
Add-TraceEvent $success '00000024' '00000001' 'FFFFFFFF' '00000013'
Add-Stage $success '00000028' '00000000' '00000013'
Add-Stage $success '00000029' '00000000' '00000013'
Add-Stage $success '0000002A' '00000000' '00000013'
Add-TraceEvent $success '00000024' '00000002' '00000000' '00000013'
Add-TraceEvent $success '00000030' '00000001' 'FFFFFFFF' '00000013'
Add-Stage $success '00000031' '00000000' '0000001F'
Add-Stage $success '00000032' '00000000' '0000001F'
Add-Stage $success '00000033' '00000000' '0000001F'
Add-Stage $success '00000034' '00000000' '0000001F'
Add-TraceEvent $success '00000030' '00000002' '00000000' '0000001F'
Add-Stage $success '00000040' '00000000' '0000001F'
Add-TraceEvent $success '00000020' '00000002' '00000000' '0000001F'
Add-Stage $success '00000027' '00000000' '0000001F'
Add-TraceEvent $success '00000002' '00000002' '00000000' '0000001F'
$successGate = '[C188R12R4R5R1_INIT] managed_main_gate=blocked reason=runtime-threadstore-attachment-unqualified singleton=0000001F gcProof=not-entered'
$successCapture = Invoke-TraceAnalysis 'a-successful-initialization' (New-TraceText $success -Gate $successGate)
Assert-True $successCapture.analysis.initTraceComplete 'successful synthetic trace is complete'
Assert-True ($successCapture.analysis.rhInitializeResult -eq 'success') 'RhInitialize success is parsed'
Assert-True $successCapture.analysis.runtimeInstanceSingletonsReady 'RuntimeInstance/ThreadStore singleton bits are visible'
Assert-True $successCapture.analysis.gcHeapAndHandlePublicationReady 'GC heap and handle manager publication bits are visible'
Assert-True (-not $successCapture.analysis.managedMainDispatched) 'candidate safety gate suppresses proof without attachment qualification'
$successStageOrder = @($successCapture.analysis.stages | ForEach-Object { $_.stageName })
Assert-True (($successStageOrder.IndexOf('PalFls') -lt $successStageOrder.IndexOf('RuntimeInstance')) -and
    ($successStageOrder.IndexOf('RuntimeInstance') -lt $successStageOrder.IndexOf('GCHeapPublication')) -and
    ($successStageOrder.IndexOf('GCHeapPublication') -lt $successStageOrder.IndexOf('GCHandleManager')) -and
    ($successStageOrder.IndexOf('GCHandleManager') -lt $successStageOrder.IndexOf('debugHeaders'))) 'actual stage order is preserved'

# B. PAL/FLS fails before the GC initialization boundary.
$early = [System.Collections.Generic.List[string]]::new()
Add-Stage $early '00000001'
Add-TraceEvent $early '00000002' '00000001'
Add-TraceEvent $early '00000010' '00000001'
Add-FailedStage $early '00000011' 'FFFFFFFF'
Add-FailedStage $early '00000010' '00000001'
Add-FailedStage $early '00000002' '00000001'
$earlyGate = '[C188R12R4R5R1_INIT] managed_main_gate=blocked rhResult=00000001'
$earlyCapture = Invoke-TraceAnalysis 'b-early-pal-failure' (New-TraceText $early -Gate $earlyGate)
Assert-True ($earlyCapture.analysis.earliestExplicitFailure.stageName -eq 'PalFls') 'early failure is attributed to PAL FLS'
Assert-True (@($earlyCapture.analysis.events | Where-Object { $_.stage -eq '00000030' }).Count -eq 0) 'early failure does not claim GC initialization began'
Assert-True $earlyCapture.analysis.managedMainSuppressedOnInitializationFailure 'ManagedMain remains suppressed after RhInitialize failure'

# C. An explicit HRESULT failure at the GC heap publication boundary.
$gcFailure = [System.Collections.Generic.List[string]]::new()
Add-Stage $gcFailure '00000001'
Add-TraceEvent $gcFailure '00000002' '00000001'
Add-Stage $gcFailure '00000010'
Add-TraceEvent $gcFailure '00000020' '00000001'
Add-Stage $gcFailure '00000024' '00000000' '00000013'
Add-TraceEvent $gcFailure '00000030' '00000001'
Add-FailedStage $gcFailure '00000031' '8007000E' '00000013'
Add-FailedStage $gcFailure '00000030' '00000001' '00000013'
Add-FailedStage $gcFailure '00000020' '00000001' '00000013'
Add-FailedStage $gcFailure '00000002' '00000001' '00000013'
$gcCapture = Invoke-TraceAnalysis 'c-gc-publication-failure' (New-TraceText $gcFailure)
Assert-True ($gcCapture.analysis.earliestExplicitFailure.stageName -eq 'GCHeapPublication') 'GC HRESULT failure is attributed to publication'
Assert-True ($gcCapture.analysis.earliestExplicitFailure.code -eq '8007000E') 'explicit HRESULT is preserved'

# D. RuntimeInstance returned, but its required ThreadStore singleton bit is absent.
$singleton = [System.Collections.Generic.List[string]]::new()
Add-Stage $singleton '00000001'
Add-TraceEvent $singleton '00000002' '00000001'
Add-TraceEvent $singleton '00000024' '00000001'
Add-TraceEvent $singleton '00000024' '00000002' '00000000' '00000001'
Add-FailedStage $singleton '00000002' '00000001' '00000001'
$singletonCapture = Invoke-TraceAnalysis 'd-missing-threadstore-singleton' (New-TraceText $singleton)
Assert-True (-not $singletonCapture.analysis.runtimeInstanceSingletonsReady) 'missing ThreadStore is reported without dereference'
Assert-True ($singletonCapture.analysis.runtimeInstanceMissingSingletonBits -eq 2) 'missing singleton bit is identified'

# E. Module registration reports the source-defined fail-fast code; no later registration stage is invented.
$moduleFailure = [System.Collections.Generic.List[string]]::new()
Add-Stage $moduleFailure '00000001'
Add-Stage $moduleFailure '00000002'
Add-TraceEvent $moduleFailure '00000060' '00000001'
Add-TraceEvent $moduleFailure '00000061' '00000001'
Add-FailedStage $moduleFailure '00000061' '0000000A'
$moduleCapture = Invoke-TraceAnalysis 'e-module-registration-failure' (New-TraceText $moduleFailure -End:$false)
Assert-True ($moduleCapture.analysis.earliestExplicitFailure.stageName -eq 'moduleRegistration') 'module failure is identified'
Assert-True (@($moduleCapture.analysis.events | Where-Object { $_.stage -eq '00000062' }).Count -eq 0) 'code-manager stage is not fabricated after module failure'
Assert-True (-not $moduleCapture.analysis.initTraceComplete) 'fail-fast without trace_end is incomplete as a whole capture'

# F. A begin marker without a return does not become an explicit stage failure.
$incomplete = [System.Collections.Generic.List[string]]::new()
Add-Stage $incomplete '00000001'
Add-TraceEvent $incomplete '00000002' '00000001'
Add-TraceEvent $incomplete '00000010' '00000001'
Add-TraceEvent $incomplete '00000011' '00000001'
$incompleteCapture = Invoke-TraceAnalysis 'f-begin-without-completion' (New-TraceText $incomplete -End:$false -TerminalNewline:$false) -TimedOut
$incompletePalFls = @($incompleteCapture.analysis.stages | Where-Object { $_.stageName -eq 'PalFls' })[0]
Assert-True ($incompletePalFls.status -eq 'begin_without_completion') 'incomplete stage is not marked failed'
Assert-True ($incompleteCapture.analysis.earliestExplicitFailure -eq $null) 'no failure code is fabricated'
Assert-True (-not $incompleteCapture.analysis.initTraceComplete) 'truncated trace is reported incomplete'

# G. Adapter failure returns before any ManagedMain dispatch.
Assert-True $earlyCapture.analysis.managedMainSuppressedOnInitializationFailure 'failure return suppresses proof entry'

# H. Synthetic successful dispatch markers are parsed as dispatch/return evidence only.
$managed = [System.Collections.Generic.List[string]]::new()
Add-Stage $managed '00000001'
Add-Stage $managed '00000002'
$managed.Add('[C188R12R4R5R1_INIT] trace_end invocation=00000001 count=00000004 overflow=00000000 serialDropped=00000000 rhResult=00000000')
$managed.Add('[C188R12R4R5R1_INIT] managed_main_gate=open rhResult=00000000')
$managed.Add('[C188R12R4R5R1_INIT] managed_main_call ordinal=00000001')
$managed.Add('[C188R12R4R5R1_INIT] managed_main_return ordinal=00000001 result=00000000')
$managedCapture = Invoke-TraceAnalysis 'h-synthetic-managed-dispatch' (($managed -join "`n") + "`n")
Assert-True $managedCapture.analysis.managedMainDispatched 'synthetic trace records dispatch after success'
Assert-True $managedCapture.analysis.managedMainReturned 'synthetic trace records return'
Assert-True (-not $managedCapture.analysis.managedMainSuppressedOnInitializationFailure) 'success path is not classified as suppressed'

# I. Raw serial bytes stay unchanged when separate derived JSON is written.
$preservationHashBefore = (Get-FileHash -LiteralPath $successCapture.serial -Algorithm SHA256).Hash
$preservedAnalysisPath = Join-Path $testRoot 'i-preservation.derived.json'
& pwsh -NoProfile -ExecutionPolicy Bypass -File $analyzer -SerialPath $successCapture.serial -OutputPath $preservedAnalysisPath | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Serial preservation analyzer invocation failed.' }
$preservationHashAfter = (Get-FileHash -LiteralPath $successCapture.serial -Algorithm SHA256).Hash
Assert-True ($preservationHashBefore -eq $preservationHashAfter) 'raw serial hash is unchanged after analysis'
Assert-True ([System.IO.Path]::GetFullPath($successCapture.serial) -ne [System.IO.Path]::GetFullPath($preservedAnalysisPath)) 'analysis output is separate from raw serial'

# J. The corrected QMP writer stays UTF-8 without a byte-order mark; no raw serial rewrite is present.
$runnerText = Get-Content -LiteralPath $runner -Raw
Assert-True ($runnerText -match '\[System\.Text\.UTF8Encoding\]::new\(\$false\)') 'QMP writer remains BOM-free'
Assert-True ($runnerText -notmatch 'Set-Content[^\r\n]*\$serial\s+-Value') 'QAC runner does not overwrite QEMU serial'
$kernelText = Get-Content -LiteralPath $kernelSource -Raw
$runtimeText = Get-Content -LiteralPath $runtimeSource -Raw
Assert-True ($kernelText.Contains('runtime-threadstore-attachment-unqualified')) 'GC proof gate is fail-closed without runtime ThreadStore attachment evidence'
Assert-True ($runtimeText.Contains('ThreadStore::GetCurrentThreadIfAvailable()')) 'singleton capture uses an accessor and avoids pointer dereference'

$manifest = [ordered]@{
    result='PASS'
    testRoot=$testRoot
    syntheticScenarios=@(
        'ordered initialization success';'PAL failure before GC';'explicit GC publication HRESULT failure'
        'RuntimeInstance returned with missing ThreadStore singleton';'module-registration fail-fast'
        'begin without completion does not fabricate failure';'RhInitialize failure suppresses ManagedMain'
        'synthetic success dispatch parsing';'raw serial hash preservation';'QMP UTF-8 BOM exclusion'
    )
    syntheticOnly=$true
    noQemuLaunched=$true
    noQacReserved=$true
    candidateManagedMainGateFailsClosed=$true
    rawSerialSha256Before=$preservationHashBefore
    rawSerialSha256After=$preservationHashAfter
    qmpEncodingUtf8WithoutBom=$true
}
$manifestPath = Join-Path $testRoot 'hosted-test-manifest.json'
[System.IO.File]::WriteAllText($manifestPath,($manifest | ConvertTo-Json -Depth 6),[System.Text.UTF8Encoding]::new($false))
Write-Output "C188R12R4R5R1 hosted attribution tests: PASS ($testRoot)"
