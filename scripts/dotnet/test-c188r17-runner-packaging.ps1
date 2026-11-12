param(
    [Parameter(Mandatory=$true)][string]$RepoRoot,
    [Parameter(Mandatory=$true)][string]$CandidateRoot,
    [Parameter(Mandatory=$true)][string]$RunRoot,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)

$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$RepoRoot=[System.IO.Path]::GetFullPath($RepoRoot)
$CandidateRoot=[System.IO.Path]::GetFullPath($CandidateRoot)
$RunRoot=[System.IO.Path]::GetFullPath($RunRoot)
$PreservedR16RunRoot=$RunRoot
$OutputDirectory=[System.IO.Path]::GetFullPath($OutputDirectory)
if (-not (Test-Path -LiteralPath $RepoRoot -PathType Container) -or
    -not (Test-Path -LiteralPath $CandidateRoot -PathType Container) -or
    -not (Test-Path -LiteralPath $RunRoot -PathType Container)) { throw 'R17 packaging rehearsal input root is missing.' }
if (Test-Path -LiteralPath $OutputDirectory) { throw "R17 packaging rehearsal refuses to overwrite output: $OutputDirectory" }
[void](New-Item -ItemType Directory -Path $OutputDirectory -Force)
$checks=[System.Collections.Generic.List[string]]::new()
function Assert-R17([bool]$Condition,[string]$Name) {
    if (-not $Condition) { throw "R17 runner packaging regression failed: $Name" }
    $script:checks.Add($Name)
}
function Get-Sha256([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Required packaging input is missing: $Path" }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}
function Get-AnalysisBoolean($Analysis,[string]$Name) { return $false }
function Test-ModuleRegistrationStageObserved($Analysis) { return $false }

$runnerPath=Join-Path $RepoRoot 'scripts\dotnet\run-c188r12r4r5-gc-ownership-qac-guest.ps1'
$admissionModulePath=Join-Path $RepoRoot 'scripts\dotnet\c188r12r4r5r4-qac-admission.psm1'
$tokens=$null;$parseErrors=$null
$runnerAst=[System.Management.Automation.Language.Parser]::ParseFile($runnerPath,[ref]$tokens,[ref]$parseErrors)
Assert-R17 ($parseErrors.Count -eq 0) 'runner parses after R17 progress and packaging changes'
$moduleTokens=$null;$moduleParseErrors=$null
$admissionModuleAst=[System.Management.Automation.Language.Parser]::ParseFile($admissionModulePath,[ref]$moduleTokens,[ref]$moduleParseErrors)
Assert-R17 ($moduleParseErrors.Count -eq 0) 'shared QAC admission module parses for runner scope handoff'
$functions=@{}
foreach($name in @('Hash-IfPresent','Capture-R13TimeoutCpuState','Capture-R16TimeoutStackWindow',
        'Complete-R13TimeoutCpuStateSerialHash','Complete-R16TimeoutStackSerialHash',
        'Assert-R17OwnedQemuChild','Capture-R17ProgressSample')) {
    $functions[$name]=$runnerAst.Find({param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name
    },$true)
}
if (@($runnerAst.FindAll({param($node)$node -is [System.Management.Automation.Language.CommandAst] -and
        $node.GetCommandName() -ceq 'Import-Module' -and $node.Extent.Text -match 'c188r12r4r5r4-qac-admission\.psm1'},$true)).Count -gt 0) {
    $functions['ConvertFrom-QmpQueryStatusResponse']=$admissionModuleAst.Find({param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'ConvertFrom-QmpQueryStatusResponse'
    },$true)
} else { $functions['ConvertFrom-QmpQueryStatusResponse']=$null }
Assert-R17 (@($functions.Values | Where-Object { $null -eq $_ }).Count -eq 0) 'all R17 invoked CPU, stack, ownership, QMP, hash, and post-exit helpers are defined in runner script scope'
$runnerText=Get-Content -LiteralPath $runnerPath -Raw
$qacExitIndex=$runnerText.IndexOf('$qacExitCode = $qacProcess.ExitCode',[System.StringComparison]::Ordinal)
$cpuSerialHashIndex=$runnerText.IndexOf('Complete-R13TimeoutCpuStateSerialHash -CapturePath',[System.StringComparison]::Ordinal)
$stackSerialHashIndex=$runnerText.IndexOf('Complete-R16TimeoutStackSerialHash -CapturePath',[System.StringComparison]::Ordinal)
Assert-R17 ($qacExitIndex -ge 0 -and $cpuSerialHashIndex -gt $qacExitIndex -and $stackSerialHashIndex -gt $cpuSerialHashIndex) 'serial hashes are completed after QAC/QEMU exit and stack serial bindings follow CPU serial hashes'
$manifestAst=$runnerAst.Find({param($node)
    $node -is [System.Management.Automation.Language.AssignmentStatementAst] -and $node.Left.Extent.Text -ceq '$manifest'
},$true)
$outcomeAst=$runnerAst.Find({param($node)
    $node -is [System.Management.Automation.Language.AssignmentStatementAst] -and $node.Left.Extent.Text -ceq '$outcome' -and
    $node.Right.Extent.Text.Contains('MANAGED_GC_ROOT_COLLECTION_VERIFIED')
},$true)
$manifestWriteCommand=$runnerAst.Find({param($node)
    $node -is [System.Management.Automation.Language.CommandAst] -and $node.GetCommandName() -ceq 'Set-Content' -and
    $node.Parent.Extent.Text -match '^\$manifest\s+\|\s+ConvertTo-Json\s+-Depth\s+10'
},$true)
$managedGcSummaryAst=$runnerAst.Find({param($node)
    $node -is [System.Management.Automation.Language.CommandAst] -and $node.GetCommandName() -ceq 'Write-Output' -and
    $node.Extent.Text.Contains('C188R13_MANAGED_GC_ROOT=$outcome')
},$true)
$managedGcSummaryBlockAst=$runnerAst.Find({param($node)
    $node -is [System.Management.Automation.Language.IfStatementAst] -and
    $node.Extent.Text.Contains('C188R13_MANAGED_GC_ROOT=$outcome')
},$true)
Assert-R17 ($null -ne $manifestAst -and $null -ne $outcomeAst -and $null -ne $manifestWriteCommand -and
    $null -ne $managedGcSummaryAst -and $null -ne $managedGcSummaryBlockAst) 'offline rehearsal uses the runner final outcome, manifest writer, and R13 completion summary AST'
$manifestCommands=@($manifestAst.Right.FindAll({param($node)$node -is [System.Management.Automation.Language.CommandAst]},$true) |
    ForEach-Object { $_.GetCommandName() } | Where-Object { $_ } | Sort-Object -Unique)
$declaredNames=@($runnerAst.FindAll({param($node)$node -is [System.Management.Automation.Language.FunctionDefinitionAst]},$true) |
    ForEach-Object { $_.Name })
$knownCommands=@('Join-Path','Select-Object','Test-Path','Where-Object') + $declaredNames
Assert-R17 (@($manifestCommands | Where-Object { $_ -notin $knownCommands }).Count -eq 0) 'every custom command called by final manifest exists before manifest generation'

$hashAst=$functions['Hash-IfPresent']
. ([scriptblock]::Create($hashAst.Extent.Text))
$convertStatusAst=$functions['ConvertFrom-QmpQueryStatusResponse']
. ([scriptblock]::Create($convertStatusAst.Extent.Text))
$assertOwnerAst=$functions['Assert-R17OwnedQemuChild']
. ([scriptblock]::Create($assertOwnerAst.Extent.Text))
$capturePairAst=$functions['Capture-R17ProgressSample']
. ([scriptblock]::Create($capturePairAst.Extent.Text))

# Run the QMP sampling orchestrator with mocked QAC/QEMU surfaces. It must verify
# ownership before both continuation and the second read, and it must stop at a
# bounded delay or fail closed without issuing QMP commands.
$script:mockQmpCommands=[System.Collections.Generic.List[string]]::new()
$script:mockSampleNumbers=[System.Collections.Generic.List[int]]::new()
$script:mockSleepSeconds=0
$script:mockIdentityValid=$true
$script:mockRunningStatus=$true
function Get-CimInstance { param($ClassName,$Filter,$ErrorAction) return [pscustomobject]@{ProcessId=404} }
function Test-QacLocalQemuChildIdentity { param($LocalProcess,$Inspection,$OwnerPid,$QemuPath,$QmpPort) return [bool]$script:mockIdentityValid }
function Invoke-Qmp { param([string]$Command,[object]$QmpArguments) $script:mockQmpCommands.Add($Command); if($Command -ceq 'query-status'){ if($script:mockRunningStatus){return '{"return":{"status":"running","running":true}}'} else{return '{"return":{"status":"paused","running":false}}'} }; return '{"return":{}}' }
function Start-Sleep { param([int]$Seconds,[int]$Milliseconds) if($Seconds){$script:mockSleepSeconds=$Seconds}; return }
function Capture-R13TimeoutCpuState { param([string]$CandidateManifestHash,[string]$SerialPath,[int]$SampleNumber=1) $script:mockSampleNumbers.Add($SampleNumber); return [pscustomobject]@{Status='captured';SampleNumber=$SampleNumber;Path=(Join-Path $runRoot 'mock-cpu-002.json');RegistersPath=(Join-Path $runRoot 'mock-cpu-002.txt');Rip='000000010005D000'} }
function Capture-R16TimeoutStackWindow { param([string]$CandidateManifestHash,[string]$RegistersPath,[string]$SerialText,[int]$SampleNumber=1) return [pscustomobject]@{Status='captured';SampleNumber=$SampleNumber;Path=(Join-Path $runRoot 'mock-stack-002.json');StackPath=(Join-Path $runRoot 'mock-stack-002.bin');LengthBytes=64;Sha256=('A'*64)} }
$qacProcess=[pscustomobject]@{HasExited=$false;Id=12}
$duringInspection=[pscustomobject]@{processId=404}
$qacReservationId='r17-test-reservation'
$qemu='qemu-mock.exe';$qmpPort=12345
$runRoot=Join-Path $OutputDirectory 'qmp-pair-success'
[void](New-Item -ItemType Directory -Path $runRoot -Force)
$pairResult=Capture-R17ProgressSample -CandidateManifestHash ('A'*64) -SerialPath (Join-Path $runRoot 'serial.log') -SerialText 'stack bounds fixture' -DelaySeconds 5
$pairMetadata=Get-Content -LiteralPath $pairResult.Path -Raw | ConvertFrom-Json
Assert-R17 ($pairResult.Status -ceq 'two-cpu-and-stack-samples-captured' -and $script:mockQmpCommands -join ',' -ceq 'cont,query-status' -and
    $script:mockSampleNumbers.Count -eq 1 -and $script:mockSampleNumbers[0] -eq 2 -and $script:mockSleepSeconds -eq 5) 'mocked QMP pair resumes once, waits five bounded seconds, then records sample 2'
Assert-R17 ($pairMetadata.ownershipBeforeResume.verified -and $pairMetadata.ownershipBeforeSecondSample.verified -and
    $pairMetadata.runningStatusAfterResume.Running -and $pairMetadata.schema -ceq 'C188R17_TIME_SEPARATED_GUEST_PROGRESS_PAIR_V1') 'sample-pair handoff records both QAC ownership gates and running-state confirmation'
$script:mockQmpCommands.Clear();$script:mockSampleNumbers.Clear();$script:mockIdentityValid=$false;$script:mockSleepSeconds=0
$runRoot=Join-Path $OutputDirectory 'qmp-pair-foreign-owner'
[void](New-Item -ItemType Directory -Path $runRoot -Force)
$blockedPair=Capture-R17ProgressSample -CandidateManifestHash ('A'*64) -SerialPath (Join-Path $runRoot 'serial.log') -SerialText '' -DelaySeconds 5
Assert-R17 ($blockedPair.Status -ceq 'failed' -and $script:mockQmpCommands.Count -eq 0 -and $script:mockSampleNumbers.Count -eq 0) 'unverified local QEMU identity blocks resume, sample, and QMP control'
$script:mockIdentityValid=$true;$script:mockRunningStatus=$false;$script:mockQmpCommands.Clear();$script:mockSampleNumbers.Clear()
$runRoot=Join-Path $OutputDirectory 'qmp-pair-still-paused'
[void](New-Item -ItemType Directory -Path $runRoot -Force)
$pausedPair=Capture-R17ProgressSample -CandidateManifestHash ('A'*64) -SerialPath (Join-Path $runRoot 'serial.log') -SerialText '' -DelaySeconds 5
Assert-R17 ($pausedPair.Status -ceq 'failed' -and $script:mockQmpCommands -join ',' -ceq 'cont,query-status' -and
    $script:mockSampleNumbers.Count -eq 0) 'failed running-state verification blocks the second CPU read'

# Extract the production Hash-IfPresent helper and the exact final-manifest AST.
# Reuse preserved R16 input files, but write four independent synthetic state
# scenarios into this new rehearsal directory only.
$variableNames=@($manifestAst.Right.FindAll({param($node)$node -is [System.Management.Automation.Language.VariableExpressionAst]},$true) |
    ForEach-Object { $_.VariablePath.UserPath } | Sort-Object -Unique)
foreach($name in $variableNames) {
    if($name -notin @('null','true','false','PID','PSCommandPath','RepoRoot','CandidateRoot','RunRoot','OutputDirectory')) {
        Set-Variable -Name $name -Value $null -Scope Script -Force
    }
}
$candidateManifestPath=Join-Path $CandidateRoot 'candidate-manifest.json'
$candidateManifest=Get-Content -LiteralPath $candidateManifestPath -Raw | ConvertFrom-Json
$candidateManifestSha256=Get-Sha256 $candidateManifestPath
$r16CpuMetaPath=Join-Path $PreservedR16RunRoot 'guest-cpu-state-timeout.json'
$r16StackMetaPath=Join-Path $PreservedR16RunRoot 'guest-stack-state-timeout.json'
$r16CpuMeta=Get-Content -LiteralPath $r16CpuMetaPath -Raw | ConvertFrom-Json
$r16StackMeta=Get-Content -LiteralPath $r16StackMetaPath -Raw | ConvertFrom-Json
$attemptPath=Join-Path (Split-Path -Parent $CandidateRoot) ('guest-launch-attempt-' + [System.IO.Path]::GetFileName($CandidateRoot) + '.json')
if(-not (Test-Path -LiteralPath $attemptPath -PathType Leaf)) { $attemptPath=Join-Path (Split-Path -Parent $CandidateRoot) 'guest-launch-attempt-candidate-r16-stack-capture-20261111T203000000Z.json' }
if(-not (Test-Path -LiteralPath $attemptPath -PathType Leaf)) { throw 'Preserved R16 one-boot guard input is missing.' }
$runnerScriptSha256=Get-Sha256 $runnerPath
$script:manifestWriteText=$manifestWriteCommand.Parent.Extent.Text
$scenarioExpected=@{
    success=@{result='NATIVEAOT_SINGLE_COLLECTION_VERIFIED';stop='managed-return-observed'}
    incomplete=@{result='MANAGED_GC_ROOT_EVIDENCE_INCOMPLETE';stop='guest-stopped-before-managed-completion'}
    fatal=@{result='NATIVEAOT_GC_BOUNDARY_NARROWED';stop='runtime-fatal'}
    timeout=@{result='MANAGED_GC_ROOT_EVIDENCE_INCOMPLETE';stop='bounded-timeout-guest-was-running'}
}
foreach($caseName in @('success','incomplete','fatal','timeout')) {
    $caseRoot=Join-Path $OutputDirectory $caseName
    [void](New-Item -ItemType Directory -Path $caseRoot -Force)
    $runRoot=$caseRoot
    $manifestPath=Join-Path $caseRoot 'guest-manifest.json'
    $serial=Join-Path $caseRoot 'serial.log'
    $qmpLog=Join-Path $caseRoot 'qmp.log'
    [System.IO.File]::Copy((Join-Path $PreservedR16RunRoot 'serial.log'),$serial,$true)
    [System.IO.File]::Copy((Join-Path $PreservedR16RunRoot 'qmp.log'),$qmpLog,$true)
    $serialText=Get-Content -LiteralPath $serial -Raw
    if($caseName -ceq 'success') { $serialText += "`n[C188R12R4R5R2R3_ENTRY_STATE] result=PASS`n" }
    if($caseName -ceq 'fatal') { $serialText += "`n[nativeaot-pal-qemu-test] FAIL_FAST reason=50414C06 detail=0000000000000000`n" }
    [System.IO.File]::WriteAllText($serial,$serialText,[System.Text.UTF8Encoding]::new($false))

    $isManagedGcRootProbe=$true
    foreach($flag in @('isGcInitFailureAttribution','isInitAttribution','isInitReturnContract','isManagedEntryProbe',
            'isManagedSettingsObjectProbe','isNativeAllocationFailureAttribution','isSafeHandoff','isSemiSharedAttribution',
            'isThreadStoreReadiness','isTransitionAttribution')) { Set-Variable -Name $flag -Value $false -Scope Script -Force }
    $sourceRamdiskPreserved=$true;$preflightPassed=$true;$admitted=$true;$releaseVerified=$true;$qacExitCode=0
    $candidateIntegrityPreservedAfterGuest=$true;$qemuFirmwareCodePreserved=$true;$uefiVarsTemplatePreserved=$true
    $transitionAttributionCaptureComplete=$caseName -in @('success','fatal')
    $managedGcTimeoutObserved=$caseName -ceq 'timeout'
    $transitionAttributionClassification=switch($caseName){
        success {'MANAGED_GC_ROOT_COLLECTION_VERIFIED'}
        fatal {'MANAGED_GC_ROOT_RUNTIME_FAIL_FAST_AFTER_ENTRY'}
        default {'MANAGED_GC_ROOT_INCOMPLETE'}
    }
    $gcStarted=$caseName -in @('success','fatal')
    $transitionAttributionAnalysis=[pscustomobject]@{
        gcCollectionRequested=$gcStarted;gcCollectionStarted=$gcStarted;gcCollectionCompleted=($caseName -ceq 'success')
        liveObjectSurvived=($caseName -ceq 'success');managedEntryReturnValue=if($caseName -ceq 'success'){'00000463'}else{$null}
        managedEntryReturned=($caseName -ceq 'success');runtimeStateReady=($caseName -ceq 'success')
        collectionCountBefore=3;collectionCountAfter=if($caseName -ceq 'success'){4}else{$null}
        postCollectionResult=if($caseName -ceq 'success'){1123}else{$null}
        transitionRecord=[pscustomobject]@{attempted='1';completed='1'}
        threadAndRuntimeState=[pscustomobject]@{threadStore='1';attachment='1'}
        callSite=[pscustomobject]@{key='RhCollect';name='RhCollect'}
        failFast=if($caseName -ceq 'fatal'){[pscustomobject]@{reason='50414C06'}}else{$null}
    }
    $managedGcAnalysisIntegrityContract=[pscustomobject]@{Valid=$true;Issues=@();RecordIntegrity='1'}
    $transitionRecordManifest=$transitionAttributionAnalysis.transitionRecord
    $transitionStateManifest=$transitionAttributionAnalysis.threadAndRuntimeState
    $transitionCallSiteManifest=$transitionAttributionAnalysis.callSite
    $transitionFailFastManifest=$transitionAttributionAnalysis.failFast
    $transitionAttributionAnalysisPath=$null;$transitionAttributionAnalysisExitCode=0;$transitionAttributionAnalysisError=$null
    $qemuStatus=if($caseName -ceq 'timeout'){[pscustomobject]@{Valid=$true;Status='running';Running=$true}}else{[pscustomobject]@{Valid=$true;Status='shutdown';Running=$false}}
    $qemuStatusResponse='mocked offline status';$qemuArgs=@('offline-rehearsal');$qemu='qemu-not-launched';$qemuSha256=$null
    $qacProcess=$null;$localGuestIdentity=$null;$qacReservationId=$null;$qacOwnerPid=$null;$qacExitCode=0
    $managedGcTimeoutCpuStateStatus=if($caseName -eq 'timeout'){'captured'}else{'not-attempted'}
    $managedGcTimeoutCpuStatePath=if($caseName -eq 'timeout'){$r16CpuMetaPath}else{$null}
    $managedGcTimeoutCpuStateRegistersPath=if($caseName -eq 'timeout'){$r16CpuMeta.registersPath}else{$null}
    $managedGcTimeoutCpuStateRip=if($caseName -eq 'timeout'){$r16CpuMeta.rip}else{$null}
    $managedGcTimeoutCpuStateError=$null;$managedGcTimeoutCpuStateSerialHashStatus=if($caseName -eq 'timeout'){$r16CpuMeta.serialHashStatus}else{'not-attempted'}
    $managedGcTimeoutCpuStateSerialHashError=$null
    $managedGcTimeoutStackCaptureStatus=if($caseName -eq 'timeout'){'captured'}else{'not-attempted'}
    $managedGcTimeoutStackCapturePath=if($caseName -eq 'timeout'){$r16StackMetaPath}else{$null}
    $managedGcTimeoutStackBinaryPath=if($caseName -eq 'timeout'){$r16StackMeta.stackPath}else{$null}
    $managedGcTimeoutStackStartAddress=if($caseName -eq 'timeout'){$r16StackMeta.startAddress}else{$null}
    $managedGcTimeoutStackLengthBytes=if($caseName -eq 'timeout'){$r16StackMeta.lengthBytesCaptured}else{0}
    $managedGcTimeoutStackSha256=if($caseName -eq 'timeout'){$r16StackMeta.stackSha256}else{$null}
    $managedGcTimeoutStackError=$null;$managedGcTimeoutStackSerialHashStatus=if($caseName -eq 'timeout'){$r16StackMeta.serialHashStatus}else{'not-attempted'}
    $managedGcTimeoutStackSerialHashError=$null
    $managedGcProgressObservationStatus=if($caseName -eq 'timeout'){'one-preserved-timeout-snapshot'}else{'not-attempted'}
    $managedGcProgressObservationDelaySeconds=if($caseName -eq 'timeout'){5}else{$null}
    $managedGcProgressObservationPath=$null;$managedGcProgressObservationError=$null
    $managedGcProgressPairMetadata=$null;$managedGcProgressQmpOwnershipVerified=$null
    $managedGcProgressCpuSamples=@();$managedGcProgressStackSamples=@()
    if($caseName -ceq 'timeout') {
        $managedGcProgressCpuSamples=@(
            [ordered]@{sampleNumber=1;schema=$r16CpuMeta.schema;status='captured';capturedAtUtc=$r16CpuMeta.capturedAtUtc;path=$r16CpuMetaPath;registersPath=$r16CpuMeta.registersPath;registersSha256=$r16CpuMeta.registersSha256;serialPath=$r16CpuMeta.serialPath;serialSha256=$r16CpuMeta.serialSha256;serialHashStatus=$r16CpuMeta.serialHashStatus;rip=$r16CpuMeta.rip;metadataSha256=(Get-Sha256 $r16CpuMetaPath);evidenceIntegrityValid=$true}
            [ordered]@{sampleNumber=2;schema='C188R17_GUEST_CPU_PROGRESS_STATE_V1';status='captured';capturedAtUtc=$r16CpuMeta.capturedAtUtc;path=$r16CpuMetaPath;registersPath=$r16CpuMeta.registersPath;registersSha256=$r16CpuMeta.registersSha256;serialPath=$r16CpuMeta.serialPath;serialSha256=$r16CpuMeta.serialSha256;serialHashStatus=$r16CpuMeta.serialHashStatus;rip=$r16CpuMeta.rip;metadataSha256=(Get-Sha256 $r16CpuMetaPath);evidenceIntegrityValid=$true}
        )
        $managedGcProgressStackSamples=@(
            [ordered]@{sampleNumber=1;schema=$r16StackMeta.schema;status='captured';capturedAtUtc=$r16StackMeta.capturedAtUtc;path=$r16StackMetaPath;stackPath=$r16StackMeta.stackPath;startAddress=$r16StackMeta.startAddress;lengthBytesRequested=$r16StackMeta.lengthBytesRequested;lengthBytesCaptured=$r16StackMeta.lengthBytesCaptured;stackSha256=$r16StackMeta.stackSha256;registersPath=$r16StackMeta.registersPath;registersSha256=$r16StackMeta.registersSha256;serialSha256=$r16StackMeta.serialSha256;serialHashStatus=$r16StackMeta.serialHashStatus;metadataSha256=(Get-Sha256 $r16StackMetaPath);evidenceIntegrityValid=$true}
            [ordered]@{sampleNumber=2;schema='C188R17_GUEST_STACK_PROGRESS_CAPTURE_V1';status='captured';capturedAtUtc=$r16StackMeta.capturedAtUtc;path=$r16StackMetaPath;stackPath=$r16StackMeta.stackPath;startAddress=$r16StackMeta.startAddress;lengthBytesRequested=$r16StackMeta.lengthBytesRequested;lengthBytesCaptured=$r16StackMeta.lengthBytesCaptured;stackSha256=$r16StackMeta.stackSha256;registersPath=$r16StackMeta.registersPath;registersSha256=$r16StackMeta.registersSha256;serialSha256=$r16StackMeta.serialSha256;serialHashStatus=$r16StackMeta.serialHashStatus;metadataSha256=(Get-Sha256 $r16StackMetaPath);evidenceIntegrityValid=$true}
        )
    }
    $candidateIntegrityInventorySha256=$null;$CandidateIntegrityInventory=$null;$candidateIntegrityAfterGuest=$null
    $candidateIntegrityPostRunError=$null;$candidateIntegrityPreservedAfterGuest=$true
    $candidateEsp=[string]$candidateManifest.esp;$uefiVarsImage=$null;$uefiVarsImageFinalSha256=$null
    $uefiVarsTemplate=$null;$uefiVarsTemplateSha256=$null;$uefiVarsTemplatePreserved=$true
    $sourceRamdiskSha256=[string]$candidateManifest.sourceRamdiskSha256;$sourceRamdiskPreserved=$true
    $candidate=$CandidateRoot;$runCorrelationId='R17-offline-'+$caseName;$runnerScriptSha256=$runnerScriptSha256
    $qemuFirmwareCodePreserved=$true;$ovmf=$null;$qemuCodeSha256=$null;$qemuFirmwareCodePreserved=$true
    $candidatePeSha256=$candidateManifest.managedPeSha256;$candidateElfSha256=$candidateManifest.managedElfSha256
    $candidateKernelSha256=$candidateManifest.specializedKernelSha256;$candidateRamdiskSha256=$candidateManifest.candidateRamdiskSha256
    $serialSha256BeforeAnalysis=Get-Sha256 $serial;$serialSha256AfterAnalysis=$serialSha256BeforeAnalysis
    $serialAnalysis=$null;$serialAnalysisError=$null;$serialAnalysisExitCode=$null;$serialAnalysisPath=$null
    $rawSerialPreservedByAnalysis=$true;$startupFailure=$false
    $transitionTerminalSeen=($caseName -eq 'success' -or $caseName -eq 'fatal');$transitionAttributionCaptureComplete=$transitionAttributionCaptureComplete
    $initCaptureComplete=$false;$threadStoreAttempt='R1';$attemptedUtc='2026-11-11T00:00:00Z'
    $managedEntryProbeProfile=$false;$managedEntryProbeAnalysisPath=$null;$managedEntryProbeAnalysisResult=$null
    $isManagedSettingsObjectProbe=$false;$isManagedEntryProbe=$false
    $qemuArguments=$qemuArgs;$guestPass=($caseName -eq 'success');$outcome=$null
    $qacBeforeStatusPath=$null;$qacBeforeListPath=$null;$qacDuringStatusPath=$null;$qacDuringListPath=$null
    $qacAfterStatusPath=$null;$qacAfterListPath=$null;$qacAfterWindowsCensusPath=$null
    $qacAfterInspectionRecords=[System.Collections.Generic.List[object]]::new()
    $beforeWindowsCensus=[pscustomobject]@{Path=$null};$beforeInspectionCapture=[pscustomobject]@{Path=$null};$duringInspectionPath=$null;$qacSnapshotRecords=@()
    $qacOwnershipIdentity=$null;$qacOwnershipIdentityPath=$null;$qmpPort=$null;$guestState=$null
    $markerLines=@();$markerPhases=@();$safeHandoffTerminal=$false;$runError=$null;$sourceRamdiskSha256=$sourceRamdiskSha256
    $managedGcAnalysisIntegrityContract=$managedGcAnalysisIntegrityContract;$managedGcTimeoutObserved=$managedGcTimeoutObserved
    $managedGcTimeoutStackCaptureMetadataPath=$managedGcTimeoutStackCapturePath
    $transitionCallSiteManifest=$transitionCallSiteManifest;$transitionFailFastManifest=$transitionFailFastManifest
    $transitionRecordManifest=$transitionRecordManifest;$transitionStateManifest=$transitionStateManifest
    $nativeAllocationAnalysis=$null;$nativeAllocationAnalysisError=$null;$nativeAllocationAnalysisExitCode=$null;$nativeAllocationAnalysisPath=$null
    $nativeAllocationClassification=$null;$nativeAllocationFirstFailure=$null;$nativeAllocationRecordCaptureComplete=$false;$nativeAllocationRecordPath=$null
    $semiSharedAnalysis=$null;$semiSharedAnalysisExitCode=$null;$semiSharedAnalysisPath=$null;$semiSharedRecordCaptureComplete=$false;$semiSharedRecordPath=$null
    $gcInitRecordCaptureComplete=$false;$gcInitRecordPath=$null;$gcInitSourceAnalysisExitCode=$null;$gcInitSourceAnalysisPath=$null;$gcInternal=$null
    $threadStoreAnalysis=$null;$threadStoreAnalysisExitCode=$null;$threadStoreAnalysisPath=$null
    $r4GcResult=[regex]::Match('', '(?m)^[C188R12R4R5-GC-OWNERSHIP] outcome=(PASS|FAIL) callbacks=(\S+) result1=(\S+) result2=(\S+) result3=(\S+)\s*$')

    Invoke-Expression $outcomeAst.Extent.Text
    Invoke-Expression $manifestAst.Extent.Text
    Invoke-Expression $manifestWriteCommand.Parent.Extent.Text
    $written=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    Assert-R17 ($written.result -ceq $scenarioExpected[$caseName].result -and $written.managedGcStopCause -ceq $scenarioExpected[$caseName].stop) "$caseName runner outcome and stop cause serialize correctly offline"
    Assert-R17 ($written.candidateIdentity.candidateManifestSha256 -ceq $candidateManifestSha256 -and
        $written.serialLogSha256 -ceq (Get-Sha256 $serial) -and $written.qmpLogSha256 -ceq (Get-Sha256 $qmpLog)) "$caseName required candidate, serial, and QMP evidence cannot be omitted"
    Assert-R17 ($written.qacReleaseVerified -eq $true) "$caseName QAC release evidence is retained independently of final manifest packaging"
    if($caseName -ceq 'timeout') {
        Assert-R17 ($written.managedGcProgressObservation.cpuSampleCount -eq 2 -and
            $written.managedGcProgressObservation.stackSampleCount -eq 2 -and
            $written.managedGcProgressObservation.cpuSamples[1].sampleNumber -eq 2 -and
            $written.managedGcProgressObservation.stackSamples[1].sampleNumber -eq 2) 'repeated CPU and stack state snapshots are retained as ordered sample records'
        Assert-R17 ($written.managedGcProgressObservation.cpuSamples[0].schema -ceq 'C188R15_GUEST_CPU_TIMEOUT_STATE_V1' -and
            $written.managedGcProgressObservation.cpuSamples[1].schema -ceq 'C188R17_GUEST_CPU_PROGRESS_STATE_V1' -and
            $written.managedGcProgressObservation.stackSamples[0].schema -ceq 'C188R16_GUEST_STACK_TIMEOUT_CAPTURE_V1' -and
            $written.managedGcProgressObservation.stackSamples[1].schema -ceq 'C188R17_GUEST_STACK_PROGRESS_CAPTURE_V1') 'R15 CPU and R16 stack records hand off explicitly to R17 sample schemas'
    }
    if($caseName -ne 'timeout') {
        Assert-R17 ($null -eq $written.managedGcProgressObservation.pairMetadataSha256) "$caseName optional absent progress metadata does not throw or synthesize a hash"
    }
}

# Rehearse the actual QAC-blocked path with no analyzer object and no one-boot
# guard. QAC release remains false because this scenario owns no reservation.
$blockedRoot=Join-Path $OutputDirectory 'qac-blocked'
[void](New-Item -ItemType Directory -Path $blockedRoot -Force)
$runRoot=$blockedRoot
$manifestPath=Join-Path $blockedRoot 'guest-manifest.json'
$attemptPath=Join-Path $blockedRoot 'one-boot-guard.json'
$serial=Join-Path $blockedRoot 'serial.log'
$qmpLog=Join-Path $blockedRoot 'qmp.log'
[System.IO.File]::Copy((Join-Path $PreservedR16RunRoot 'serial.log'),$serial,$true)
[System.IO.File]::Copy((Join-Path $PreservedR16RunRoot 'qmp.log'),$qmpLog,$true)
$serialText=Get-Content -LiteralPath $serial -Raw
$serialSha256BeforeAnalysis=Get-Sha256 $serial
$serialSha256AfterAnalysis=$serialSha256BeforeAnalysis
$isManagedGcRootProbe=$true
$preflightPassed=$false;$admitted=$false;$releaseVerified=$false;$qacExitCode=$null
$qacProcess=$null;$qemuStatus=$null;$qemuStatusResponse=$null
$managedGcTimeoutObserved=$false
$managedGcTimeoutCpuStateStatus='not-attempted';$managedGcTimeoutCpuStatePath=$null
$managedGcTimeoutCpuStateRegistersPath=$null;$managedGcTimeoutCpuStateRip=$null;$managedGcTimeoutCpuStateError=$null
$managedGcTimeoutCpuStateSerialHashStatus='not-attempted';$managedGcTimeoutCpuStateSerialHashError=$null
$managedGcTimeoutStackCaptureStatus='not-attempted';$managedGcTimeoutStackCapturePath=$null
$managedGcTimeoutStackBinaryPath=$null;$managedGcTimeoutStackStartAddress=$null;$managedGcTimeoutStackLengthBytes=0
$managedGcTimeoutStackSha256=$null;$managedGcTimeoutStackError=$null
$managedGcTimeoutStackSerialHashStatus='not-attempted';$managedGcTimeoutStackSerialHashError=$null
$managedGcProgressObservationStatus='not-attempted';$managedGcProgressObservationDelaySeconds=$null
$managedGcProgressObservationPath=$null;$managedGcProgressObservationError=$null
$managedGcProgressPairMetadata=$null;$managedGcProgressQmpOwnershipVerified=$false
$managedGcProgressCpuSamples=@();$managedGcProgressStackSamples=@()
$transitionAttributionAnalysis=$null;$transitionAttributionAnalysisPath=$null
$transitionAttributionAnalysisExitCode=$null;$transitionAttributionAnalysisError=$null
$transitionAttributionClassification='MANAGED_GC_ROOT_INCOMPLETE';$transitionAttributionCaptureComplete=$false
$transitionTerminalSeen=$false;$guestPass=$false;$managedGcAnalysisIntegrityContract=$null
$transitionRecordManifest=$null;$transitionStateManifest=$null;$transitionCallSiteManifest=$null;$transitionFailFastManifest=$null
$candidateIntegrityPreservedAfterGuest=$true;$qemuFirmwareCodePreserved=$true;$uefiVarsTemplatePreserved=$true
$sourceRamdiskPreserved=$true;$runError='BLOCKED_QAC_PREFLIGHT: coordinator/census is not online, empty, and internally consistent.'
$outcome=$null;$qacBeforeStatusPath=$null;$qacBeforeListPath=$null;$qacDuringStatusPath=$null;$qacDuringListPath=$null
$qacAfterStatusPath=$null;$qacAfterListPath=$null;$qacAfterWindowsCensusPath=$null
$qacAfterInspectionRecords=[System.Collections.Generic.List[object]]::new()
$qacSnapshotRecords=@();$beforeWindowsCensus=[pscustomobject]@{Path=$null};$beforeInspectionCapture=[pscustomobject]@{Path=$null}
$duringInspectionPath=$null;$qacOwnershipIdentity=$null;$qacOwnershipIdentityPath=$null;$qacReservationId=$null
$qacOwnerPid=$null;$localGuestIdentity=$null;$qmpPort=$null
$qemu='qemu-not-launched';$qemuArgs=@('not-launched');$qemuArguments=$qemuArgs;$qemuSha256=$null
$candidate=$CandidateRoot;$candidateIntegrityInventorySha256=$null;$CandidateIntegrityInventory=$null
$candidateIntegrityAfterGuest=$null;$candidateIntegrityPostRunError=$null
$candidateEsp=[string]$candidateManifest.esp;$sourceRamdiskSha256=[string]$candidateManifest.sourceRamdiskSha256
$uefiVarsImage=$null;$uefiVarsImageFinalSha256=$null;$uefiVarsTemplate=$null;$uefiVarsTemplateSha256=$null
$candidatePeSha256=$candidateManifest.managedPeSha256;$candidateElfSha256=$candidateManifest.managedElfSha256
$candidateKernelSha256=$candidateManifest.specializedKernelSha256;$candidateRamdiskSha256=$candidateManifest.candidateRamdiskSha256
$ovmf=$null;$qemuCodeSha256=$null;$qemuFirmwareCodePreserved=$true
$runnerScriptSha256=Get-Sha256 $runnerPath;$runCorrelationId='R17-offline-qac-blocked'
$serialAnalysis=$null;$serialAnalysisPath=$null;$serialAnalysisExitCode=$null;$serialAnalysisError=$null
$rawSerialPreservedByAnalysis=$true;$startupFailure=$false;$markerLines=@();$markerPhases=@();$safeHandoffTerminal=$false
$transitionAttributionAnalysisPath=$null;$transitionAttributionAnalysisExitCode=$null;$transitionAttributionAnalysisError=$null
$transitionAttributionClassification='MANAGED_GC_ROOT_INCOMPLETE';$transitionAttributionCaptureComplete=$false
$transitionAttributionAnalysis=$null;$transitionTerminalSeen=$false;$transitionRecordManifest=$null
$transitionStateManifest=$null;$transitionCallSiteManifest=$null;$transitionFailFastManifest=$null
$qacAfterInspectionRecords=[System.Collections.Generic.List[object]]::new()
$nativeAllocationAnalysis=$null;$nativeAllocationAnalysisError=$null;$nativeAllocationAnalysisExitCode=$null;$nativeAllocationAnalysisPath=$null
$nativeAllocationClassification=$null;$nativeAllocationFirstFailure=$null;$nativeAllocationRecordCaptureComplete=$false;$nativeAllocationRecordPath=$null
$semiSharedAnalysis=$null;$semiSharedAnalysisExitCode=$null;$semiSharedAnalysisPath=$null;$semiSharedRecordCaptureComplete=$false;$semiSharedRecordPath=$null
$gcInitRecordCaptureComplete=$false;$gcInitRecordPath=$null;$gcInitSourceAnalysisExitCode=$null;$gcInitSourceAnalysisPath=$null;$gcInternal=$null
$threadStoreAnalysis=$null;$threadStoreAnalysisExitCode=$null;$threadStoreAnalysisPath=$null
$r4GcResult=[regex]::Match('', '(?m)^[C188R12R4R5-GC-OWNERSHIP] outcome=(PASS|FAIL) callbacks=(\S+) result1=(\S+) result2=(\S+) result3=(\S+)\s*$')

Invoke-Expression $outcomeAst.Extent.Text
Invoke-Expression $manifestAst.Extent.Text
Invoke-Expression $manifestWriteCommand.Parent.Extent.Text
$blockedManifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
Assert-R17 ($blockedManifest.result -ceq 'NATIVEAOT_GC_HOSTED_READY_GUEST_DEFERRED' -and
    -not $blockedManifest.qacPreflightPassed -and -not $blockedManifest.qacAdmissionGranted -and
    -not $blockedManifest.qacReleaseVerified -and -not (Test-Path -LiteralPath $attemptPath)) 'QAC-blocked closeout writes a deferred manifest without claiming a reservation, release, or consumed one-boot guard'

# The final R13 status line also runs when QAC blocks admission and no analyzer
# object exists. Exercise the exact production summary block for that path.
$transitionAttributionAnalysis=$null
$outcome='NATIVEAOT_GC_HOSTED_READY_GUEST_DEFERRED'
$admitted=$false
$transitionAttributionCaptureComplete=$false
$transitionAttributionClassification='MANAGED_GC_ROOT_INCOMPLETE'
$releaseVerified=$false
$isManagedGcRootProbe=$true
$manifestPath=Join-Path $OutputDirectory 'blocked-summary-manifest.json'
$blockedSummary=Invoke-Expression $managedGcSummaryBlockAst.Extent.Text
$blockedSummaryLine=@($blockedSummary | Where-Object { $_ -match 'ADMITTED=False .*GC_STARTED=False GC_COMPLETED=False LIVE_OBJECT=False RETURN= QAC_RELEASE=False' })
Assert-R17 ($blockedSummaryLine.Count -eq 1) 'QAC-blocked runner summary handles absent collection-analysis fields without breaking final packaging'

$report=[ordered]@{
    schema='C188R17_RUNNER_PACKAGING_HOSTED_TESTS_V1'
    result='PASS'
    assertionCount=$checks.Count
    assertions=@($checks.ToArray())
    rehearsalCases=@('success','incomplete','fatal','timeout','qac-blocked')
    preservedR16Candidate=$CandidateRoot
    preservedR16RunRoot=$PreservedR16RunRoot
    outputDirectory=$OutputDirectory
    hostedEvidenceOnly=$true
    qemuLaunched=$false
    qacReserved=$false
    nativeGcAcceptance=$false
}
[System.IO.File]::WriteAllText((Join-Path $OutputDirectory 'runner-packaging-hosted-tests.json'),
    ($report | ConvertTo-Json -Depth 10),[System.Text.UTF8Encoding]::new($false))
Write-Output ("R17 runner packaging hosted tests: PASS assertions={0}; scenarios=5; qemuLaunched=false; qacReserved=false" -f $checks.Count)
