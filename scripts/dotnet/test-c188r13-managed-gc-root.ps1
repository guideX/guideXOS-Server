param(
    [string]$RepoRoot=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$RuntimePackManifest,
    [Parameter(Mandatory=$true)][string]$ManagedMap,
    [Parameter(Mandatory=$true)][string]$ManagedMainDisassembly,
    [Parameter(Mandatory=$true)][string]$GcCollectWrapperDisassembly,
    [Parameter(Mandatory=$true)][string]$ConstructorDisassembly,
    [Parameter(Mandatory=$true)][string]$CalculateDisassembly,
    [Parameter(Mandatory=$true)][string]$KernelBuildLog,
    [Parameter(Mandatory=$true)][string]$ProductionNativeAotMap,
    [Parameter(Mandatory=$true)][string]$R11HostedTestResults,
    [Parameter(Mandatory=$true)][string]$CallSiteMapPath
)

$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=[System.IO.Path]::GetFullPath($RepoRoot)
$checks=[System.Collections.Generic.List[object]]::new()
function Assert-R13([bool]$Condition,[string]$Name) {
    if (-not $Condition) { throw "R13 hosted assertion failed: $Name" }
    $checks.Add([ordered]@{name=$Name;passed=$true})
}
foreach ($path in @($RuntimePackManifest,$ManagedMap,$ManagedMainDisassembly,$GcCollectWrapperDisassembly,
    $ConstructorDisassembly,$CalculateDisassembly,$KernelBuildLog,$ProductionNativeAotMap,
    $R11HostedTestResults,$CallSiteMapPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "R13 hosted input is missing: $path" }
}
$sourcePath=Join-Path $root 'samples\managed\HostLogProof\ManagedGcRootProbe.cs'
$projectPath=Join-Path $root 'samples\managed\HostLogProof\HostLogProof.csproj'
$kernelPath=Join-Path $root 'kernel\core\nativeaot_pal_qemu_test.cpp'
$counterPath=Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_platform.cpp'
$headerPath=Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_managed_gc_root_probe.h'
$runnerPath=Join-Path $root 'scripts\dotnet\run-c188r12r4r5-gc-ownership-qac-guest.ps1'
$analyzerPath=Join-Path $root 'scripts\dotnet\analyze-c188r13-managed-gc-root.ps1'
$transitionAnalyzerPath=Join-Path $root 'scripts\dotnet\analyze-c188r12r4r5r2r3-transition-attribution.ps1'
$r12AnalyzerPath=Join-Path $root 'scripts\dotnet\analyze-c188r12r4r5r4r5r2r2r4r2r3r12-managed-settings-object.ps1'
$candidateBuilderPath=Join-Path $root 'scripts\dotnet\build-c188r12r4r5r1-init-attribution-candidate.ps1'
$admissionModulePath=Join-Path $root 'scripts\dotnet\c188r12r4r5r4-qac-admission.psm1'
foreach ($path in @($sourcePath,$projectPath,$kernelPath,$counterPath,$headerPath,$runnerPath,
    $analyzerPath,$transitionAnalyzerPath,$r12AnalyzerPath,$candidateBuilderPath,$admissionModulePath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "R13 source-contract input is missing: $path" }
}
$source=Get-Content -LiteralPath $sourcePath -Raw
$candidateBuilder=Get-Content -LiteralPath $candidateBuilderPath -Raw
$admissionModule=Get-Content -LiteralPath $admissionModulePath -Raw
$project=Get-Content -LiteralPath $projectPath -Raw
$map=Get-Content -LiteralPath $ManagedMap -Raw
$main=Get-Content -LiteralPath $ManagedMainDisassembly -Raw
$gcCollectWrapper=Get-Content -LiteralPath $GcCollectWrapperDisassembly -Raw
$ctor=Get-Content -LiteralPath $ConstructorDisassembly -Raw
$calculate=Get-Content -LiteralPath $CalculateDisassembly -Raw
$kernel=Get-Content -LiteralPath $kernelPath -Raw
$kernelLog=Get-Content -LiteralPath $KernelBuildLog -Raw
$counter=Get-Content -LiteralPath $counterPath -Raw
$header=Get-Content -LiteralPath $headerPath -Raw
$runner=Get-Content -LiteralPath $runnerPath -Raw
$analyzer=Get-Content -LiteralPath $analyzerPath -Raw
$manifest=Get-Content -LiteralPath $RuntimePackManifest -Raw | ConvertFrom-Json
$r11=Get-Content -LiteralPath $R11HostedTestResults -Raw | ConvertFrom-Json
$rhCollectMapMatch=[regex]::Match($map,'(?m)^\s*0001:[0-9A-Fa-f]{8}\s+S_P_CoreLib_System_Runtime_InternalCalls__RhCollect\s+(?<address>[0-9A-Fa-f]{16})\b')
$rhpCollectMapMatch=[regex]::Match($map,'(?m)^\s*0001:[0-9A-Fa-f]{8}\s+RhpCollect\s+(?<address>[0-9A-Fa-f]{16})\s+f(?:\s|$)')
$calculateMapMatch=[regex]::Match($map,'(?m)^\s*0001:[0-9A-Fa-f]{8}\s+(?<symbol>\S*DisplaySettingsProbe__Calculate\S*)\s+(?<address>[0-9A-Fa-f]{16})\b')
$reversePInvokeMapMatch=[regex]::Match($map,'(?m)^\s*0001:[0-9A-Fa-f]{8}\s+RhpReversePInvoke\s+(?<address>[0-9A-Fa-f]{16})\s+f\s+guidexos_nativeaot_platform\.obj\b')
$newFastMapMatch=[regex]::Match($map,'(?m)^\s*0001:[0-9A-Fa-f]{8}\s+RhpNewFast\s+(?<address>[0-9A-Fa-f]{16})\s+f\s+guidexos_nativeaot_platform\.obj\b')

Assert-R13 ($manifest.runtimeIdentity.sourceCommit -ceq '9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3' -and
    $manifest.runtimeIdentity.gc -ceq 'Workstation' -and [bool]$manifest.c188r12r4r5GcOwnershipProof) 'Pinned NativeAOT Workstation GC identity and ownership profile'
Assert-R13 (-not [bool]$manifest.imageBackedArenaPresent -and [int64]$manifest.managedHeapBytes -eq 0) 'Diagnostic candidate uses the real GC heap without the image-backed arena'
Assert-R13 ($source -match 'sealed class DisplaySettingsProbe' -and
    $source -match 'public static unsafe class ManagedSettingsObjectProbe') 'Dedicated R13 source retains the known managed settings object contract'
Assert-R13 ($source -match 'new\(640, 480, 2\)' -and $source -match 'settings\.Calculate\(\)') 'One real managed object is constructed and initially read'
Assert-R13 ([regex]::Matches($source,'GC\.Collect\(\)').Count -eq 1 -and
    $source -notmatch 'WaitForPendingFinalizers') 'Exactly one synchronous explicit collection and no finalizer wait'
Assert-R13 ($source -match 'ReadNativeCounters\(' -and
    $source -match 'guideXosNativeAotC188R12R4R5ReadCounters') 'Managed probe reads native GC counters on both sides of collection'
Assert-R13 ($source -match 'countAfter > countBefore' -and $source -match 'CounterStatusAfter') 'Managed completion requires a real collection-count increase and successful native post-read'
$collectPosition=$source.IndexOf('GC.Collect()', [StringComparison]::Ordinal)
$mutatePosition=$source.IndexOf('settings.Scale = 3', [StringComparison]::Ordinal)
$postReadPosition=$source.LastIndexOf('settings.Calculate()', [StringComparison]::Ordinal)
$keepAlivePosition=$source.IndexOf('GC.KeepAlive(settings)', [StringComparison]::Ordinal)
Assert-R13 ($collectPosition -ge 0 -and $collectPosition -lt $mutatePosition -and
    $mutatePosition -lt $postReadPosition -and $postReadPosition -lt $keepAlivePosition) 'Same live managed local is mutated and read after collection before KeepAlive'
Assert-R13 ($source -match '\[MethodImpl\(MethodImplOptions\.NoInlining\)\]\s+public DisplaySettingsProbe' -and
    $source -match '\[MethodImpl\(MethodImplOptions\.NoInlining\)\]\s+public int Calculate') 'Constructor and field consumer remain separate compiled methods'
Assert-R13 ($source -notmatch '~DisplaySettingsProbe|GCHandle|Thread\(|Task\.|ThreadPool|while\s*\(|for\s*\(') 'No finalizer, explicit handle, managed worker, or pressure loop is introduced'
Assert-R13 ($source -notmatch 'IntPtr|void\*\s+settings|GCHandle' -and
    $source -match 'GC.KeepAlive\(settings\)') 'Native record stores scalars and no native object or interior pointer'
Assert-R13 ($project -match "HostLogProofC188R13ManagedGcRootProbe\)' == 'true'" -and
    $project -match '<Compile Include="ManagedGcRootProbe.cs" />' -and
    $project -match '<Compile Remove="ManagedGcRootProbe.cs" />') 'R13 source compiles only under its explicit diagnostic property'
Assert-R13 ($candidateBuilder -match 'C188R13_GUEST_PHASE_PLAN_V1' -and
    $candidateBuilder -match 'exactly one synchronous GC\.Collect' -and
    $candidateBuilder -match 'native collection-count advance and completion evidence' -and
    $candidateBuilder -match 'C188R13_ONE_BOOT_GUARD_TEMPLATE_V1') 'R13 candidate plan and one-boot template describe the collection and live-root admission contract'
Assert-R13 ($admissionModule -match "NativeAotManagedGcRootProbe" -and
    $admissionModule -match "C188R13_GUEST_PHASE_PLAN_V1" -and
    $admissionModule -match "C188R13_ONE_BOOT_GUARD_TEMPLATE_V1" -and
    $admissionModule -match 'candidateGcCollectWrapperDisassembly' -and
    $admissionModule -match 'transitionQacAdmissionModule') 'Strict QAC admission validates R13 profile, linked collection path, and its own pinned module hash'
Assert-R13 ($map -match 'ManagedSettingsObjectProbe.*ManagedMain' -and
    $map -match 'DisplaySettingsProbe.*___ctor' -and $map -match 'DisplaySettingsProbe.*__Calculate' -and
    $map -match 'RhpNewFast' -and $map -match 'RhpCollect' -and
    $map -match 'guideXosNativeAotC188R12R4R5ReadCounters') 'Linked NativeAOT image includes entry, allocation, collection, counter, constructor, and field consumer'
Assert-R13 ($rhCollectMapMatch.Success -and $rhpCollectMapMatch.Success -and
    $main -match ('call\s+0x' + ([Convert]::ToUInt64($rhCollectMapMatch.Groups['address'].Value,16)).ToString('x') + '\b') -and
    $gcCollectWrapper -match ('call\s+0x' + ([Convert]::ToUInt64($rhpCollectMapMatch.Groups['address'].Value,16)).ToString('x') + '\b')) 'Linked GC.Collect call reaches the pinned RhpCollect helper through its compiled wrapper'
if ($rhCollectMapMatch.Success -and $calculateMapMatch.Success) {
    $collectCallPosition=$main.IndexOf(('call   0x' + ([Convert]::ToUInt64($rhCollectMapMatch.Groups['address'].Value,16)).ToString('x')),[StringComparison]::OrdinalIgnoreCase)
    $postCollectionMutationPosition=$main.IndexOf('mov    DWORD PTR [rsi+0x10],0x3',$collectCallPosition,[StringComparison]::OrdinalIgnoreCase)
    $postCollectionCalculatePosition=$main.IndexOf(('call   0x' + ([Convert]::ToUInt64($calculateMapMatch.Groups['address'].Value,16)).ToString('x')),$postCollectionMutationPosition,[StringComparison]::OrdinalIgnoreCase)
} else { $collectCallPosition=-1; $postCollectionMutationPosition=-1; $postCollectionCalculatePosition=-1 }
Assert-R13 ($map -match '\.managedcode\$I' -and
    $map -match 'EnumerateLiveSlots' -and $map -match 'ReportSlotToGC' -and
    $main -match 'mov    rsi,rax' -and $collectCallPosition -ge 0 -and
    $postCollectionMutationPosition -gt $collectCallPosition -and
    $postCollectionCalculatePosition -gt $postCollectionMutationPosition) 'Generated NativeAOT code metadata and GC slot decoder are linked; the same RSI object root is changed and read after RhCollect'
Assert-R13 ($main -match '52313345' -and $main -match '52313351' -and
    $main -match '52313343' -and $main -match '52313352' -and
    $main -match 'call\s+0x[0-9a-fA-F]+') 'Linked ManagedMain retains the R13 protocol markers and executable calls'
Assert-R13 ($main -match 'RhpCollect|RhpNewFast|DisplaySettingsProbe' -or
    $main -match 'call\s+0x[0-9a-fA-F]+') 'ManagedMain disassembly retains calls across collection and object access'
$reversePInvokeCallTarget=[regex]::Replace($reversePInvokeMapMatch.Groups['address'].Value,'^0+','').ToLowerInvariant()
$newFastCallTarget=[regex]::Replace($newFastMapMatch.Groups['address'].Value,'^0+','').ToLowerInvariant()
$reversePInvokeCallIndex=$main.IndexOf(('call   0x{0}' -f $reversePInvokeCallTarget),[StringComparison]::OrdinalIgnoreCase)
$managedEntryStoreIndex=$main.IndexOf('52313345',[StringComparison]::OrdinalIgnoreCase)
$newFastCallIndex=$main.IndexOf(('call   0x{0}' -f $newFastCallTarget),[StringComparison]::OrdinalIgnoreCase)
Assert-R13 ($reversePInvokeMapMatch.Success -and $newFastMapMatch.Success -and
    $reversePInvokeCallIndex -ge 0 -and $managedEntryStoreIndex -gt $reversePInvokeCallIndex -and
    $newFastCallIndex -gt $managedEntryStoreIndex) 'Generated method calls mapped RhpReversePInvoke before its first body marker and calls mapped RhpNewFast afterward'
$entryMarkerPosition=$source.IndexOf('record->EntryMarker = EntryMarker',[StringComparison]::Ordinal)
$allocationPosition=$source.IndexOf('DisplaySettingsProbe settings = new(640, 480, 2)',[StringComparison]::Ordinal)
$initialReadPosition=$source.IndexOf('int initial = settings.Calculate()',[StringComparison]::Ordinal)
$initialResultPosition=$source.IndexOf('record->InitialResult',[StringComparison]::Ordinal)
$counterBeforePosition=$source.IndexOf('int beforeStatus = ReadNativeCounters',[StringComparison]::Ordinal)
$requestPosition=$source.IndexOf('record->RequestMarker = RequestMarker',[StringComparison]::Ordinal)
$collectPosition=$source.IndexOf('GC.Collect()',[StringComparison]::Ordinal)
$counterAfterPosition=$source.IndexOf('int afterStatus = ReadNativeCounters',[StringComparison]::Ordinal)
$mutatePosition=$source.IndexOf('settings.Scale = 3',[StringComparison]::Ordinal)
$finalReadPosition=$source.IndexOf('int result = settings.Calculate()',[StringComparison]::Ordinal)
$keepAlivePosition=$source.IndexOf('GC.KeepAlive(settings)',[StringComparison]::Ordinal)
$returnPosition=$source.IndexOf('return result',[StringComparison]::Ordinal)
Assert-R13 ($entryMarkerPosition -ge 0 -and $entryMarkerPosition -lt $allocationPosition -and
    $allocationPosition -lt $initialReadPosition -and $initialReadPosition -lt $initialResultPosition -and
    $initialResultPosition -lt $counterBeforePosition -and $counterBeforePosition -lt $requestPosition -and
    $requestPosition -lt $collectPosition -and $collectPosition -lt $counterAfterPosition -and
    $counterAfterPosition -lt $mutatePosition -and $mutatePosition -lt $finalReadPosition -and
    $finalReadPosition -lt $keepAlivePosition -and $keepAlivePosition -lt $returnPosition) 'R13 source order is entry, allocation, initial 1122, pre-count, request, collection, post-count, mutation, final read, KeepAlive, return'
Assert-R13 ($ctor -match '5231334[fF]' -and $ctor -match '(?m)^\s*[0-9a-fA-F]{8,16}:') 'Constructor machine code stores the R13 construction witness'
Assert-R13 ($calculate -match '(?m)^\s*[0-9a-fA-F]{8,16}:') 'Field consumer has generated machine instructions'
Assert-R13 ($kernelLog -match '-DGXOS_NATIVEAOT_C188R13_MANAGED_GC_ROOT_PROBE' -and
    $kernel -match 'C188R13_GC_ROOT_NATIVE' -and $kernel -match 'collectionCountAfter > probeRecord.collectionCountBefore') 'Native kernel validates a real collection count delta before guest pass'
Assert-R13 ($header -match 'sizeof\(GuideXosNativeAotManagedGcRootProbeRecord\) == 44u' -and
    $source -match 'Size = 44') 'Managed and native probe record layouts agree at 44 bytes'
Assert-R13 ($counter -match 'gcInProgress != 0u' -and $counter -match 'guideXosNativeAotC188R12R4R5ReadCounters') 'Native counter API rejects reads during collection and requires initialized heap/thread state'
Assert-R13 ($runner -match 'CreateNew' -and $runner -match 'automaticRetry=\$false' -and
    $runner -match 'NATIVEAOT_SINGLE_COLLECTION_VERIFIED' -and $runner -match 'gcCollectionCompleted' -and
    $runner -match 'Get-R13ManagedGcAnalysisIntegrityContract' -and
    $runner -match 'managedGcAnalysisIntegrityContract\.RecordIntegrity' -and
    $runner -match 'managedGcAnalysisIntegrityContract\.TransitionParseError' -and
    $analyzer -match 'recordIntegrity=\[string\]\$transition\.recordIntegrity' -and
    $analyzer -match 'transitionParseError=\[string\]\$transition\.parseError' -and
    $runner.Contains('if ($isManagedGcRootProbe) {') -and
    $runner.Contains('Join-Path $phaseRoot ("guest-launch-attempt-" + [System.IO.Path]::GetFileName($candidate) + ".json")') -and
    $candidateBuilder.Contains('Join-Path $phaseRoot ("guest-launch-attempt-" + [System.IO.Path]::GetFileName($candidate) + ".json")') -and
    $admissionModule -match '\$expectedGuardPath') 'QAC runner, phase plan, and admission checks bind the candidate-specific one-boot guard path and require native collection completion'
Assert-R13 ($runner -match 'Capture-R13TimeoutCpuState' -and
    $runner -match 'Complete-R13TimeoutCpuStateSerialHash' -and
    $runner -match 'Capture-R16TimeoutStackWindow' -and
    $runner -match 'Complete-R16TimeoutStackSerialHash' -and
    $runner -match 'memsave 0x' -and $runner -match 'guest-virtual' -and
    $runner -match 'managedGcTimeoutStackCaptureMetadataSha256=Hash-IfPresent' -and
    $runner -match 'managedGcTimeoutStackSerialHashStatus=\$managedGcTimeoutStackSerialHashStatus' -and
    $runner -match 'serialHashStatus=''deferred-until-qemu-exit''' -and
    $runner -match 'verified-after-qemu-exit' -and
    $runner -match "human-monitor-command" -and $runner -match 'info registers' -and
    $runner -match "Invoke-Qmp 'stop'" -and
    $runner -match '\$isManagedGcRootProbe -and -not \$transitionTerminalSeen -and\s+\$qemuStatus\.Valid -and \[bool\]\$qemuStatus\.Running' -and
    $runner -match 'managedGcTimeoutObserved=\$managedGcTimeoutObserved' -and
    $runner -match "RIP=\(\[0-9A-Fa-f\]\+\)") 'R15 runner takes a bounded exact-candidate CPU snapshot only after an incomplete R13 timeout'
$cpuCaptureCallIndex=$runner.IndexOf('Capture-R13TimeoutCpuState -CandidateManifestHash',[StringComparison]::Ordinal)
$ownedQemuCheckIndex=$runner.LastIndexOf('Test-QacLocalQemuChildIdentity',$cpuCaptureCallIndex,[StringComparison]::Ordinal)
$qmpQuitAfterCaptureIndex=$runner.IndexOf("Invoke-Qmp 'quit'",$cpuCaptureCallIndex,[StringComparison]::Ordinal)
$qacExitWaitIndex=$runner.IndexOf('$qacProcess.WaitForExit(90000)',$cpuCaptureCallIndex,[StringComparison]::Ordinal)
$serialHashFinalizeIndex=$runner.IndexOf('Complete-R13TimeoutCpuStateSerialHash -CapturePath',$cpuCaptureCallIndex,[StringComparison]::Ordinal)
$stackSerialHashFinalizeIndex=$runner.IndexOf('Complete-R16TimeoutStackSerialHash -CapturePath',$cpuCaptureCallIndex,[StringComparison]::Ordinal)
Assert-R13 ($cpuCaptureCallIndex -ge 0 -and $ownedQemuCheckIndex -ge 0 -and
    $ownedQemuCheckIndex -lt $cpuCaptureCallIndex -and $qmpQuitAfterCaptureIndex -gt $cpuCaptureCallIndex -and
    $qacExitWaitIndex -gt $qmpQuitAfterCaptureIndex -and $serialHashFinalizeIndex -gt $qacExitWaitIndex -and
    $stackSerialHashFinalizeIndex -gt $qacExitWaitIndex) 'Timeout CPU and stack snapshots are ownership-gated and serial hashing waits for QAC process exit'
$r13ScriptTokens=$null; $r13ScriptParseErrors=$null
$runnerAst=[System.Management.Automation.Language.Parser]::ParseFile($runnerPath,[ref]$r13ScriptTokens,[ref]$r13ScriptParseErrors)
$integrityFunctionAst=$runnerAst.Find({param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -ceq 'Get-R13ManagedGcAnalysisIntegrityContract'
},$true)
Assert-R13 ($r13ScriptParseErrors.Count -eq 0 -and $null -ne $integrityFunctionAst) 'R15 QMP and analyzer-schema runner changes parse as PowerShell'
$captureCpuStateAst=$runnerAst.Find({param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -ceq 'Capture-R13TimeoutCpuState'
},$true)
$completeCpuStateAst=$runnerAst.Find({param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -ceq 'Complete-R13TimeoutCpuStateSerialHash'
},$true)
$stackBoundsAst=$runnerAst.Find({param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -ceq 'Get-R16StackBoundsObservation'
},$true)
$captureStackAst=$runnerAst.Find({param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -ceq 'Capture-R16TimeoutStackWindow'
},$true)
$completeStackHashAst=$runnerAst.Find({param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -ceq 'Complete-R16TimeoutStackSerialHash'
},$true)
$hashIfPresentAst=$runnerAst.Find({param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -ceq 'Hash-IfPresent'
},$true)
Assert-R13 ($null -ne $captureCpuStateAst -and $null -ne $completeCpuStateAst -and
    $null -ne $stackBoundsAst -and $null -ne $captureStackAst -and $null -ne $completeStackHashAst -and
    $null -ne $hashIfPresentAst) 'R15/R16 capture and final-manifest hash helper functions are isolated in the runner'
if ($null -ne $captureCpuStateAst -and $null -ne $completeCpuStateAst -and
    $null -ne $stackBoundsAst -and $null -ne $captureStackAst -and $null -ne $completeStackHashAst -and
    $null -ne $hashIfPresentAst) {
    . ([scriptblock]::Create($captureCpuStateAst.Extent.Text))
    . ([scriptblock]::Create($completeCpuStateAst.Extent.Text))
    . ([scriptblock]::Create($stackBoundsAst.Extent.Text))
    . ([scriptblock]::Create($captureStackAst.Extent.Text))
    . ([scriptblock]::Create($completeStackHashAst.Extent.Text))
    . ([scriptblock]::Create($hashIfPresentAst.Extent.Text))
    $script:mockStackMemsaveCalls=0
    $script:mockStackMemsaveTruncate=$false
    $script:mockStackMemsaveFailure=$false
    $script:mockStackMemsaveFailureResponse=$false
    function Invoke-Qmp([string]$Command,[System.Collections.IDictionary]$QmpArguments=$null) {
        switch ($Command) {
            'stop' { return '{"return":{}}' }
            'query-status' { return '{"return":{"status":"paused","running":false}}' }
            'human-monitor-command' {
                $hmp=[string]$QmpArguments['command-line']
                if ($hmp -match '^memsave 0x[0-9A-Fa-f]+ 0x(?<length>[0-9A-Fa-f]+) "(?<path>.*)"$') {
                    $script:mockStackMemsaveCalls++
                    if ($script:mockStackMemsaveFailure) { throw 'Synthetic bounded memsave failure.' }
                    $byteCount=[Convert]::ToInt32($Matches.length,16)
                    if ($script:mockStackMemsaveTruncate) { $byteCount=[Math]::Max(0,$byteCount-8) }
                    $mockStackPath=$Matches.path.Replace('/','\')
                    [System.IO.File]::WriteAllBytes($mockStackPath,[byte[]]::new($byteCount))
                    if ($script:mockStackMemsaveFailureResponse) { return '{"return":"Could not write bounded test stack"}' }
                    return '{"return":"Saved bounded test stack window"}'
                }
                return '{"return":"RIP=0000000100057ecc RSP=00000000052b5200 RBP=00000000052b5360"}'
            }
            default { throw "Unexpected mocked QMP command: $Command" }
        }
    }
    function ConvertFrom-QmpQueryStatusResponse([string]$Text) {
        return [pscustomobject]@{ Valid=$true; Running=$false; Status='paused' }
    }
    function Get-Sha256([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
    $captureMockRoot=Join-Path $OutputDirectory ('r15-cpu-capture-mock-' + [guid]::NewGuid().ToString('N'))
    [void](New-Item -ItemType Directory -Path $captureMockRoot -Force)
    $runRoot=$captureMockRoot
    $serialMockPath=Join-Path $captureMockRoot 'serial.log'
    $serialMockStream=[System.IO.FileStream]::new($serialMockPath,[System.IO.FileMode]::CreateNew,
        [System.IO.FileAccess]::ReadWrite,[System.IO.FileShare]::None)
    try {
        $serialMockBytes=[System.Text.Encoding]::UTF8.GetBytes('synthetic serial evidence held exclusively')
        $serialMockStream.Write($serialMockBytes,0,$serialMockBytes.Length)
        $serialMockStream.Flush()
        $mockCapture=Capture-R13TimeoutCpuState -CandidateManifestHash ('a'*64) -SerialPath $serialMockPath
        $mockCaptureJson=Get-Content -LiteralPath $mockCapture.Path -Raw | ConvertFrom-Json
        Assert-R13 ($mockCapture.Status -ceq 'captured' -and $mockCaptureJson.serialHashStatus -ceq 'deferred-until-qemu-exit' -and
            $null -eq $mockCaptureJson.serialSha256 -and $mockCaptureJson.rip -ceq '0000000100057ECC') 'Exclusive serial lock does not prevent the paused QMP register snapshot'
    } finally { $serialMockStream.Dispose() }
    Assert-R13 ((Hash-IfPresent (Join-Path $captureMockRoot 'absent-stack-sidecar.json')) -eq $null -and
        (Hash-IfPresent $mockCapture.RegistersPath) -ceq (Get-Sha256 $mockCapture.RegistersPath)) `
        'Final manifest hash helper returns null for an absent sidecar and hashes a present capture'
    $mockSerialFinalization=Complete-R13TimeoutCpuStateSerialHash -CapturePath $mockCapture.Path -SerialPath $serialMockPath
    $mockCaptureFinal=Get-Content -LiteralPath $mockCapture.Path -Raw | ConvertFrom-Json
    Assert-R13 ($mockSerialFinalization.Status -ceq 'verified-after-qemu-exit' -and
        $mockCaptureFinal.serialSha256 -ceq (Get-Sha256 $serialMockPath)) 'Serial hash is attached only after the serial file lock is released'
    $missingSerialCapture=Join-Path $captureMockRoot 'missing-serial-capture.json'
    Copy-Item -LiteralPath $mockCapture.Path -Destination $missingSerialCapture
    $missingSerialResult=Complete-R13TimeoutCpuStateSerialHash -CapturePath $missingSerialCapture `
        -SerialPath (Join-Path $captureMockRoot 'missing-serial.log')
    $missingSerialCaptureJson=Get-Content -LiteralPath $missingSerialCapture -Raw | ConvertFrom-Json
    Assert-R13 ($missingSerialResult.Status -ceq 'unavailable-after-qemu-exit' -and
        $missingSerialCaptureJson.status -ceq 'captured' -and
        $missingSerialCaptureJson.registersSha256 -ceq (Get-Sha256 $mockCapture.RegistersPath)) 'Serial hash failure preserves the completed CPU register evidence'

    $validBoundsSerial='[C188R12R4R5R2R3_HANDOFF] checkpoint=before_managed_main status=00000000 stackReady=00000001 stackInBounds=00000001 stackLow=0000000000100000 stackHigh=0000000000102000 result=PASS'
    $validBounds=Get-R16StackBoundsObservation $validBoundsSerial
    Assert-R13 ($validBounds.Valid -and $validBounds.Low -eq 0x100000 -and $validBounds.High -eq 0x102000) 'R16 stack window accepts only a passing bounded ThreadStore stack observation'
    $r16SuccessRoot=Join-Path $captureMockRoot 'r16-success'
    [void](New-Item -ItemType Directory -Path $r16SuccessRoot -Force)
    $runRoot=$r16SuccessRoot
    $r16RegisterPath=Join-Path $r16SuccessRoot 'registers.txt'
    Set-Content -LiteralPath $r16RegisterPath -Value "CPU#0`nRIP=0000000100057ECC RSP=0000000000101FF0 RBP=0000000000101FF8" -Encoding ascii
    $stackCapture=Capture-R16TimeoutStackWindow -CandidateManifestHash ('b'*64) `
        -RegistersPath $r16RegisterPath -SerialText $validBoundsSerial
    $stackCaptureJson=Get-Content -LiteralPath $stackCapture.Path -Raw | ConvertFrom-Json
    Assert-R13 ($stackCapture.Status -ceq 'captured' -and $stackCapture.LengthBytes -eq 16 -and
        $stackCapture.StartAddress -ceq '0000000000101FF0' -and
        $stackCaptureJson.guestCpuIndex -eq 0 -and $stackCaptureJson.status -ceq 'captured' -and
        $stackCaptureJson.stackSha256 -ceq (Get-Sha256 $stackCapture.StackPath) -and
        $mockStackMemsaveCalls -eq 1) 'R16 memsave captures the exact RSP-to-stack-high virtual range and binds its hash'
    $r16MaximumRoot=Join-Path $captureMockRoot 'r16-maximum-window'
    [void](New-Item -ItemType Directory -Path $r16MaximumRoot -Force)
    $runRoot=$r16MaximumRoot
    Set-Content -LiteralPath $r16RegisterPath -Value "CPU#0`nRIP=0000000100057ECC RSP=0000000000100000 RBP=0000000000100008" -Encoding ascii
    $maximumWindowCapture=Capture-R16TimeoutStackWindow -CandidateManifestHash ('b'*64) `
        -RegistersPath $r16RegisterPath -SerialText $validBoundsSerial
    Assert-R13 ($maximumWindowCapture.Status -ceq 'captured' -and $maximumWindowCapture.LengthBytes -eq 8192) `
        'R16 memsave never exceeds the fixed 8192-byte window when the whole stack range is available'
    $stackHashFinal=Complete-R16TimeoutStackSerialHash -CapturePath $stackCapture.Path `
        -HashStatus 'verified-after-qemu-exit' -SerialSha256 ('c'*64)
    $stackCaptureFinal=Get-Content -LiteralPath $stackCapture.Path -Raw | ConvertFrom-Json
    Assert-R13 ($stackHashFinal.Status -ceq 'verified-after-qemu-exit' -and
        $stackCaptureFinal.serialSha256 -ceq (('c'*64).ToUpperInvariant())) 'R16 stack metadata binds serial evidence only after QAC exit'
    $unavailableStackHash=Complete-R16TimeoutStackSerialHash -CapturePath $maximumWindowCapture.Path `
        -HashStatus 'unavailable-after-qemu-exit' -SerialSha256 $null
    $unavailableStackJson=Get-Content -LiteralPath $maximumWindowCapture.Path -Raw | ConvertFrom-Json
    Assert-R13 ($unavailableStackHash.Status -ceq 'unavailable-after-qemu-exit' -and
        $unavailableStackJson.status -ceq 'captured' -and $null -eq $unavailableStackJson.serialSha256 -and
        $unavailableStackJson.serialHashError) 'R16 post-exit serial hash failure preserves captured stack bytes and records the hash gap'

    $r16MissingRoot=Join-Path $captureMockRoot 'r16-missing-bounds'
    [void](New-Item -ItemType Directory -Path $r16MissingRoot -Force)
    $runRoot=$r16MissingRoot
    $beforeMissingBoundsCalls=$mockStackMemsaveCalls
    $missingBoundsCapture=Capture-R16TimeoutStackWindow -CandidateManifestHash ('b'*64) `
        -RegistersPath $r16RegisterPath -SerialText '[C188R12R4R5R2R3_HANDOFF] status=00000000 stackReady=00000000 stackInBounds=00000000'
    $missingBoundsJson=Get-Content -LiteralPath $missingBoundsCapture.Path -Raw | ConvertFrom-Json
    Assert-R13 ($missingBoundsCapture.Status -ceq 'unavailable-stack-bounds-missing' -and
        $missingBoundsJson.status -ceq 'unavailable-stack-bounds-missing' -and
        $mockStackMemsaveCalls -eq $beforeMissingBoundsCalls) 'R16 does not read guest memory when valid stack-bound metadata is missing'

    $r16InvalidRoot=Join-Path $captureMockRoot 'r16-invalid-rsp'
    [void](New-Item -ItemType Directory -Path $r16InvalidRoot -Force)
    $runRoot=$r16InvalidRoot
    Set-Content -LiteralPath $r16RegisterPath -Value "CPU#0`nRIP=0000000100057ECC RSP=0000000000102000 RBP=0000000000101FF8" -Encoding ascii
    $beforeInvalidRspCalls=$mockStackMemsaveCalls
    $invalidRspCapture=Capture-R16TimeoutStackWindow -CandidateManifestHash ('b'*64) `
        -RegistersPath $r16RegisterPath -SerialText $validBoundsSerial
    Assert-R13 ($invalidRspCapture.Status -ceq 'unavailable-rsp-outside-reported-stack' -and
        $mockStackMemsaveCalls -eq $beforeInvalidRspCalls) 'R16 rejects an RSP outside the recorded stack bounds before memsave'

    $r16TruncatedRoot=Join-Path $captureMockRoot 'r16-incomplete-memsave'
    [void](New-Item -ItemType Directory -Path $r16TruncatedRoot -Force)
    $runRoot=$r16TruncatedRoot
    Set-Content -LiteralPath $r16RegisterPath -Value "CPU#0`nRIP=0000000100057ECC RSP=0000000000101FF0 RBP=0000000000101FF8" -Encoding ascii
    $script:mockStackMemsaveTruncate=$true
    $incompleteStackCapture=Capture-R16TimeoutStackWindow -CandidateManifestHash ('b'*64) `
        -RegistersPath $r16RegisterPath -SerialText $validBoundsSerial
    $script:mockStackMemsaveTruncate=$false
    $incompleteStackJson=Get-Content -LiteralPath $incompleteStackCapture.Path -Raw | ConvertFrom-Json
    Assert-R13 ($incompleteStackCapture.Status -ceq 'incomplete' -and
        $incompleteStackJson.lengthBytesCaptured -eq 8 -and $incompleteStackJson.lengthBytesRequested -eq 16 -and
        $incompleteStackJson.registersSha256 -ceq (Get-Sha256 $r16RegisterPath)) 'R16 incomplete memsave preserves register evidence and reports the short stack read'
    $failedMemsaveRoot=Join-Path $captureMockRoot 'r16-failed-memsave'
    [void](New-Item -ItemType Directory -Path $failedMemsaveRoot -Force)
    $runRoot=$failedMemsaveRoot
    $script:mockStackMemsaveFailure=$true
    $failedStackCapture=Capture-R16TimeoutStackWindow -CandidateManifestHash ('b'*64) `
        -RegistersPath $r16RegisterPath -SerialText $validBoundsSerial
    $script:mockStackMemsaveFailure=$false
    $failedStackJson=Get-Content -LiteralPath $failedStackCapture.Path -Raw | ConvertFrom-Json
    Assert-R13 ($failedStackCapture.Status -ceq 'failed' -and
        $failedStackJson.status -ceq 'failed' -and $failedStackJson.registersSha256 -ceq (Get-Sha256 $r16RegisterPath) -and
        $failedStackJson.error -match 'Synthetic bounded memsave failure') 'R16 failed stack reads retain CPU-register provenance and report the bounded-read failure'
    $errorResponseRoot=Join-Path $captureMockRoot 'r16-error-response'
    [void](New-Item -ItemType Directory -Path $errorResponseRoot -Force)
    $runRoot=$errorResponseRoot
    $script:mockStackMemsaveFailureResponse=$true
    $errorResponseCapture=Capture-R16TimeoutStackWindow -CandidateManifestHash ('b'*64) `
        -RegistersPath $r16RegisterPath -SerialText $validBoundsSerial
    $script:mockStackMemsaveFailureResponse=$false
    $errorResponseJson=Get-Content -LiteralPath $errorResponseCapture.Path -Raw | ConvertFrom-Json
    Assert-R13 ($errorResponseCapture.Status -ceq 'failed' -and
        $errorResponseJson.lengthBytesCaptured -eq 16 -and
        $errorResponseJson.error -match 'QEMU memsave reported a failure') 'R16 rejects an HMP-reported memsave failure even when a same-sized host file exists'
}
$r13LoopStart=$runner.IndexOf('if ($isManagedGcRootProbe) {',$runner.IndexOf('$deadline = (Get-Date).AddSeconds($BootTimeoutSeconds)',[StringComparison]::Ordinal),[StringComparison]::Ordinal)
$r13LoopGeneric=$runner.IndexOf('} elseif ($isTransitionAttribution) {',$r13LoopStart,[StringComparison]::Ordinal)
$r13LoopBlock=if($r13LoopStart -ge 0 -and $r13LoopGeneric -gt $r13LoopStart){$runner.Substring($r13LoopStart,$r13LoopGeneric-$r13LoopStart)}else{''}
Assert-R13 ($r13LoopBlock -match 'Get-R13ManagedGcTerminalObservation' -and
    $r13LoopBlock -notmatch 'C188R12R4R5R2R3_TRANSITION' -and
    $runner -match '(?s)function Get-R13ManagedGcTerminalObservation.*?FAIL_FAST.*?C188R12R4R5R2R3_ENTRY_STATE.*result=\(\?:PASS\|FAIL\)') 'R13 runner waits for the post-ManagedMain state marker and cannot stop on the earlier InitializeModules-return marker'
$runnerTokens=$null; $runnerParseErrors=$null
$runnerAst=[System.Management.Automation.Language.Parser]::ParseFile($runnerPath,[ref]$runnerTokens,[ref]$runnerParseErrors)
$terminalFunctionAst=$runnerAst.Find({param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -ceq 'Get-R13ManagedGcTerminalObservation'
},$true)
Assert-R13 ($runnerParseErrors.Count -eq 0 -and $null -ne $terminalFunctionAst) 'Corrected R13 terminal classifier parses as an isolated runner function'
. ([scriptblock]::Create($terminalFunctionAst.Extent.Text))
$integrityFunctionAst=$runnerAst.Find({param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -ceq 'Get-R13ManagedGcAnalysisIntegrityContract'
},$true)
. ([scriptblock]::Create($integrityFunctionAst.Extent.Text))
$r15FixtureRoot=Join-Path $root 'scripts\dotnet\fixtures\c188r15'
$r13IntermediateFixture=Get-Content -Raw -LiteralPath (Join-Path $r15FixtureRoot 'r13-initialize-modules-intermediate.serial.log')
$r14AttachmentFixture=Get-Content -Raw -LiteralPath (Join-Path $r15FixtureRoot 'r14-post-attachment-last-marker.serial.log')
$r14OldAnalysisFixture=Get-Content -Raw -LiteralPath (Join-Path $r15FixtureRoot 'r14-pre-fix-managed-gc-analysis.json') | ConvertFrom-Json
$r14OldTransitionFixture=Get-Content -Raw -LiteralPath (Join-Path $r15FixtureRoot 'r14-pre-fix-transition-analysis.json') | ConvertFrom-Json
$r14PostFixAnalysisFixture=Get-Content -Raw -LiteralPath (Join-Path $r15FixtureRoot 'r14-post-fix-managed-gc-analysis-schema.json') | ConvertFrom-Json
$r13IntermediateObservation=Get-R13ManagedGcTerminalObservation $r13IntermediateFixture
$r14AttachmentObservation=Get-R13ManagedGcTerminalObservation $r14AttachmentFixture
$r14OldSchemaContract=Get-R13ManagedGcAnalysisIntegrityContract $r14OldAnalysisFixture
Assert-R13 (-not $r13IntermediateObservation.terminal -and $r13IntermediateObservation.kind -ceq 'Continuing') 'R13 regression fixture keeps InitializeModules-return as an intermediate runner event'
Assert-R13 (-not $r14AttachmentObservation.terminal -and $r14AttachmentObservation.kind -ceq 'Continuing' -and
    $r14AttachmentFixture.TrimEnd().EndsWith('event=transition_attach_complete raw0=0000000003D79360 raw1=0000000000000001')) 'R14 regression fixture preserves attachment as the last observed event without inventing managed entry'
Assert-R13 (-not $r14OldSchemaContract.Valid -and $r14OldSchemaContract.RecordIntegrity -eq $null -and
    $r14OldSchemaContract.Issues -contains 'top-level-recordIntegrity-missing' -and
    $r14OldSchemaContract.Issues -contains 'top-level-transitionParseError-missing' -and
    $r14OldTransitionFixture.recordIntegrity -ceq '1') 'R14 pre-fix split-schema fixture is rejected without copying integrity from the separate transition record'
$r13State='[C188R12R4R5R2R3_ENTRY_STATE] status=00000000 bytes=00000070 membership=00000001 flags=00000001 cooperativeState=00000001 preemptiveFrame=00000001 flsMatch=00000001 threadId=0000000000001234 observedThreadId=0000000000001234 stackReady=00000001 stackInBounds=00000001 modules=00000001 codeManager=00000001 currentThread=0000000000005678 palFlsThread=0000000000005678 frame=FFFFFFFFFFFFFFFF stackLow=0000000000100000 stackHigh=0000000000200000 stackCurrent=0000000000180000 result=PASS'
$r13Attempt='[C188R13_GC_ROOT_ENTRY] event=attempt abi=UnmanagedCallersOnly-Cdecl seed=0000000E symbol=ManagedMain model=DisplaySettingsProbe collection=one_synchronous_full_request'
$r13Allocation='[C188R12R4R5R4R5R2R2R4R2R3R12_ALLOC] helper=RhpNewFast stock=guideXosStockRhpNewFast requests=00000001 completed=00000001 objectSize=00000018 contextReadyBefore=00000001 contextReadyAfter=00000001 contextBytesBefore=00001000 contextBytesAfter=00000FE8 contextAdvanced=00000018 methodTable=00000001 gcHeapOwned=00000001 threadAttached=00000001 gcCountBefore=00000000 gcCountAfter=00000000 status=00000000 collectionRequestedByProbe=00000000'
$r13Returned='[C188R13_GC_ROOT_ENTRY] event=returned entry=52313345 input=0000000E initial=00000462 request=52313351 counterStatusBefore=00000000 countBefore=00000000 countAfter=00000001 counterStatusAfter=00000000 completion=52313343 final=00000463 returnMarker=52313352 return=00000463 expected=00000463 record=PASS collection=requested'
$r13Native='[C188R13_GC_ROOT_NATIVE] request=00000001 counterStatusBefore=00000000 countBefore=00000000 countAfter=00000001 counterStatusAfter=00000000 completed=00000001 delta=00000001 result=PASS'
$r13MilestoneSnapshots=@(
    '[nativeaot-pal-qemu-test] runtime_initialized=1',
    ('[nativeaot-pal-qemu-test] runtime_initialized=1' + "`n" + '[C188R12R4R5R2R3_TRANSITION] result=initialize_modules_returned managedEntry=probe_follows collection=not_requested'),
    ('[C188R12R4R5R2R3_TRANSITION] result=initialize_modules_returned managedEntry=probe_follows collection=not_requested' + "`n" + $r13Attempt),
    ($r13Attempt + "`n" + $r13Allocation),
    ($r13Attempt + "`n" + $r13Allocation + "`n" + $r13Returned + "`n" + $r13Native),
    ($r13Attempt + "`n" + $r13Allocation + "`n" + $r13Returned + "`n" + $r13Native + "`n" + $r13State)
)
$r13Observations=@($r13MilestoneSnapshots | ForEach-Object { Get-R13ManagedGcTerminalObservation $_ })
Assert-R13 (@($r13Observations | Where-Object { $_.terminal }).Count -eq 1 -and
    $r13Observations[-1].kind -ceq 'PostReturnRuntimeState') 'Successful initialization continues through managed attempt, allocation, collection, return, and post-return checks'
$fatalBeforeAllocation=Get-R13ManagedGcTerminalObservation ($r13Attempt + "`n" + '[nativeaot-pal-qemu-test] FAIL_FAST reason=50414C06 detail=0000000000000000')
$fatalInCollection=Get-R13ManagedGcTerminalObservation ($r13Attempt + "`n" + $r13Allocation + "`n" + '[C188R13_TEST_CONTEXT] stage=collection' + "`n" + '[nativeaot-pal-qemu-test] FAIL_FAST reason=50414C06 detail=0000000000000000')
Assert-R13 ($fatalBeforeAllocation.terminal -and $fatalBeforeAllocation.kind -ceq 'FailFast' -and
    $fatalInCollection.terminal -and $fatalInCollection.kind -ceq 'FailFast') 'Explicit fatal before allocation and during the collection context is terminal immediately'
$allocationFailureSerial=$r13Attempt + "`n" + '[C188R13_TEST_CONTEXT] stage=allocation helper=RhpNewFast' + "`n" +
    '[nativeaot-pal-qemu-test] FAIL_FAST reason=50414C06 detail=0000000000000000'
$fatalInAllocation=Get-R13ManagedGcTerminalObservation $allocationFailureSerial
Assert-R13 ($fatalBeforeAllocation.terminal -and $fatalBeforeAllocation.kind -ceq 'FailFast' -and
    $fatalInAllocation.terminal -and $fatalInAllocation.kind -ceq 'FailFast' -and
    $allocationFailureSerial.IndexOf('stage=allocation helper=RhpNewFast',[StringComparison]::Ordinal) -lt
    $allocationFailureSerial.IndexOf('FAIL_FAST',[StringComparison]::Ordinal)) 'Synthetic pre-allocation and in-allocation fatal contexts remain distinct terminal observations'
$noTerminalObservation=Get-R13ManagedGcTerminalObservation ($r13Attempt + "`n" + $r13Allocation + "`n" + $r13Returned + "`n" + $r13Native)
Assert-R13 (-not $noTerminalObservation.terminal -and $noTerminalObservation.kind -ceq 'Continuing' -and
    $runner -match '\$deadline = \(Get-Date\)\.AddSeconds\(\$BootTimeoutSeconds\)' -and
    $runner -match '\[ValidateRange\(60,300\)\]\[int\]\$BootTimeoutSeconds') 'Missing post-return state stays incomplete under a bounded runner timeout'
Assert-R13 ($runner.IndexOf("[void](Invoke-Qmp 'quit')",[StringComparison]::Ordinal) -gt
    $runner.IndexOf('$deadline = (Get-Date).AddSeconds($BootTimeoutSeconds)',[StringComparison]::Ordinal) -and
    $runner -match 'Refusing QMP shutdown because the local QEMU child identity is no longer verified') 'QMP shutdown remains after terminal/timeout polling and behind ownership verification'
Assert-R13 ($analyzer -match 'attemptOrder -ge \$allocationOrder' -and
    $analyzer -match 'gcCollectionCompleted' -and $analyzer -match 'liveObjectSurvived' -and
    $analyzer -match 'evidencePreserved' -and $analyzer -match 'stateRecords') 'R13 analyzer gates on event order, collection, liveness, multiple state snapshots, and immutable raw evidence'
Assert-R13 ($r11.result -ceq 'PASS' -and -not [bool]$r11.qemuLaunched -and
    -not [bool]$r11.qacReserved) 'Accepted R11 managed-entry hosted regression remains green without QAC use'
Assert-R13 (-not $map.Contains('g_guideXosManagedHeap') -and
    -not $map.Contains('C188R13') -and -not $map.Contains('ManagedGcRootProbe')) 'Proof-disabled production NativeAOT map excludes R13 probe and diagnostic controls'

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$fixtureRoot=Join-Path ([System.IO.Path]::GetFullPath($OutputDirectory)) 'analyzer-fixtures'
New-Item -ItemType Directory -Force -Path $fixtureRoot | Out-Null
$entryPrefix='[C188R13_GC_ROOT_ENTRY] '
$alloc='[C188R12R4R5R4R5R2R2R4R2R3R12_ALLOC] helper=RhpNewFast stock=guideXosStockRhpNewFast requests=00000001 completed=00000001 objectSize=00000018 contextReadyBefore=00000001 contextReadyAfter=00000001 contextBytesBefore=00001000 contextBytesAfter=00000FE8 contextAdvanced=00000018 methodTable=00000001 gcHeapOwned=00000001 threadAttached=00000001 gcCountBefore=00000000 gcCountAfter=00000000 status=00000000 collectionRequestedByProbe=00000000'
$attempt=$entryPrefix+'event=attempt abi=UnmanagedCallersOnly-Cdecl seed=0000000E symbol=ManagedMain model=DisplaySettingsProbe collection=one_synchronous_full_request'
$returned=$entryPrefix+'event=returned entry=52313345 input=0000000E initial=00000462 request=52313351 counterStatusBefore=00000000 countBefore=00000000 countAfter=00000001 counterStatusAfter=00000000 completion=52313343 final=00000463 returnMarker=52313352 return=00000463 expected=00000463 record=PASS collection=requested'
$native='[C188R13_GC_ROOT_NATIVE] request=00000001 counterStatusBefore=00000000 countBefore=00000000 countAfter=00000001 counterStatusAfter=00000000 completed=00000001 delta=00000001 result=PASS'
$state='[C188R12R4R5R2R3_ENTRY_STATE] status=00000000 bytes=00000070 membership=00000001 flags=00000001 cooperativeState=00000001 preemptiveFrame=00000001 flsMatch=00000001 threadId=0000000000001234 observedThreadId=0000000000001234 stackReady=00000001 stackInBounds=00000001 modules=00000001 codeManager=00000001 currentThread=0000000000005678 palFlsThread=0000000000005678 frame=FFFFFFFFFFFFFFFF stackLow=0000000000100000 stackHigh=0000000000200000 stackCurrent=0000000000180000 result=PASS'
function Invoke-R13Fixture([string]$Name,[string[]]$Lines) {
    $path=Join-Path $fixtureRoot ($Name+'.serial.log')
    $out=Join-Path $fixtureRoot ($Name+'.analysis.json')
    [System.IO.File]::WriteAllLines($path,$Lines,[System.Text.UTF8Encoding]::new($false))
    & pwsh -NoProfile -ExecutionPolicy Bypass -File $analyzerPath -SerialPath $path `
        -CallSiteMapPath $CallSiteMapPath -OutputPath $out | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "R13 fixture analyzer failed to preserve $Name." }
    return (Get-Content -LiteralPath $out -Raw | ConvertFrom-Json)
}
$valid=Invoke-R13Fixture 'valid-order-marker-only' @($attempt,$alloc,$returned,$native,$state)
$afterAttachLines=@($r14AttachmentFixture -split "\r?\n" | Where-Object { $_ -ne '' }) +
    @($alloc,$returned,$native,$state)
$afterAttach=Invoke-R13Fixture 'synthetic-managed-progression-after-attachment' $afterAttachLines
$validIntegrityContract=Get-R13ManagedGcAnalysisIntegrityContract $r14PostFixAnalysisFixture
Assert-R13 ($valid.managedEntryReturned -and $valid.gcCollectionStarted -and
    $valid.gcCollectionCompleted -and $valid.liveObjectSurvived -and
    ($valid.PSObject.Properties.Name -contains 'recordIntegrity') -and
    ($valid.PSObject.Properties.Name -contains 'transitionParseError') -and
    [string]::IsNullOrWhiteSpace([string]$valid.parseError) -and
    $valid.classification -ceq 'MANAGED_GC_ROOT_RETURNED_MISMATCH') 'Positive ordering fixture parses complete collection and liveness markers without claiming a guest pass'
Assert-R13 ($afterAttach.managedEntryAttempted -and $afterAttach.managedEntryReturned -and
    $afterAttach.allocationSucceeded -and $afterAttach.gcCollectionStarted -and
    $afterAttach.gcCollectionCompleted -and $afterAttach.liveObjectSurvived -and
    $afterAttach.evidencePreserved -and $afterAttach.classification -cne 'MANAGED_GC_ROOT_COLLECTION_VERIFIED') 'Synthetic progression after attachment exercises every analyzer milestone without claiming native qualification'
Assert-R13 ($validIntegrityContract.Valid -and $validIntegrityContract.RecordIntegrity -ceq '1' -and
    [string]::IsNullOrWhiteSpace([string]$validIntegrityContract.TransitionParseError)) 'Corrected R14 analyzer schema exposes top-level integrity and transition-parse evidence'
$malformedSchema=$r14PostFixAnalysisFixture | ConvertTo-Json -Depth 12 | ConvertFrom-Json
$malformedSchema.recordIntegrity='true'
$malformedSchemaContract=Get-R13ManagedGcAnalysisIntegrityContract $malformedSchema
$contradictorySchema=$r14PostFixAnalysisFixture | ConvertTo-Json -Depth 12 | ConvertFrom-Json
$contradictorySchema.transitionParseError='record parser rejected a malformed row'
$contradictorySchemaContract=Get-R13ManagedGcAnalysisIntegrityContract $contradictorySchema
Assert-R13 (-not $malformedSchemaContract.Valid -and $malformedSchemaContract.Issues -contains 'top-level-recordIntegrity-malformed' -and
    -not $contradictorySchemaContract.Valid -and $contradictorySchemaContract.Issues -contains 'recordIntegrity-contradicts-transitionParseError') 'Malformed and contradictory recordIntegrity schemas are rejected fail-closed'
$multipleStates=Invoke-R13Fixture 'multiple-valid-post-return-state-snapshots' @($attempt,$alloc,$returned,$native,$state,$state)
Assert-R13 ($multipleStates.managedRuntimeStateSnapshotCount -eq 2 -and
    $multipleStates.runtimeStateReady -and
    $multipleStates.classification -ceq 'MANAGED_GC_ROOT_RETURNED_MISMATCH') 'Multiple legitimate post-return state snapshots are retained and validated'
$missingState=Invoke-R13Fixture 'missing-post-return-state' @($attempt,$alloc,$returned,$native)
Assert-R13 ($missingState.classification -ceq 'MANAGED_GC_ROOT_INCOMPLETE' -and
    [string]$missingState.parseError -ceq 'r13_event_order_contract_mismatch') 'Missing post-return state evidence remains incomplete'
$missingCollectionCompletion=Invoke-R13Fixture 'missing-native-collection-completion' @($attempt,$alloc,$returned,$state)
Assert-R13 ($missingCollectionCompletion.classification -ceq 'MANAGED_GC_ROOT_INCOMPLETE' -and
    [string]$missingCollectionCompletion.parseError -ceq 'r13_event_order_contract_mismatch') 'Managed result without native collection completion remains incomplete'
$allocationWithoutReturn=Invoke-R13Fixture 'allocation-record-without-managed-return' @($attempt,$alloc)
Assert-R13 ($allocationWithoutReturn.managedEntryAttempted -and
    -not $allocationWithoutReturn.managedEntryReturned -and
    -not $allocationWithoutReturn.allocationSucceeded -and
    -not $allocationWithoutReturn.gcCollectionStarted -and
    $allocationWithoutReturn.classification -ceq 'MANAGED_GC_ROOT_INCOMPLETE') 'Evidence ending after allocation but before managed return or GC remains incomplete'
$reordered=Invoke-R13Fixture 'allocation-after-return' @($attempt,$returned,$alloc,$native,$state)
Assert-R13 ($reordered.classification -ceq 'MANAGED_GC_ROOT_INCOMPLETE' -and
    [string]$reordered.parseError -ceq 'r13_event_order_contract_mismatch') 'Negative ordering fixture rejects allocation evidence after managed return'
$zeroDelta=Invoke-R13Fixture 'no-native-collection-delta' @($attempt,$alloc,
    ($returned -replace 'countAfter=00000001','countAfter=00000000'),
    ($native -replace 'countAfter=00000001','countAfter=00000000' -replace 'delta=00000001','delta=00000000'),$state)
Assert-R13 (-not $zeroDelta.gcCollectionStarted -and -not $zeroDelta.gcCollectionCompleted -and
    $zeroDelta.classification -ceq 'MANAGED_GC_ROOT_RETURNED_MISMATCH') 'Negative fixture rejects a claimed 1123 result without a native collection-count increase'
Assert-R13 ($valid.managedEntryReturned -and [string]::IsNullOrWhiteSpace([string]$valid.parseError) -and
    $missingCollectionCompletion.managedEntryReturned -and
    $missingCollectionCompletion.classification -ceq 'MANAGED_GC_ROOT_INCOMPLETE') 'A managed return record remains distinct from an incomplete end-to-end collection record'
$contradictory=Invoke-R13Fixture 'contradictory-runtime-state' @($attempt,$alloc,$returned,$native,
    ($state -replace 'membership=00000001','membership=00000000'))
Assert-R13 (-not $contradictory.runtimeStateReady -and
    $contradictory.classification -ceq 'MANAGED_GC_ROOT_RETURNED_MISMATCH') 'Negative fixture rejects contradictory post-return ThreadStore state'
$duplicate=Invoke-R13Fixture 'duplicate-return-marker' @($attempt,$alloc,$returned,$native,$state,$returned)
Assert-R13 ($duplicate.classification -ceq 'MANAGED_GC_ROOT_INCOMPLETE' -and
    [string]$duplicate.parseError -ceq 'duplicate_r13_probe_marker') 'Negative fixture rejects duplicate return evidence'

$r12Attempt='[C188R12R4R5R4R5R2R2R4R2R3R12_ENTRY] event=attempt abi=UnmanagedCallersOnly-Cdecl seed=0000000E symbol=ManagedMain model=DisplaySettingsProbe collection=not_requested'
$r12Alloc='[C188R12R4R5R4R5R2R2R4R2R3R12_ALLOC] helper=RhpNewFast stock=guideXosStockRhpNewFast requests=00000001 completed=00000001 objectSize=00000018 contextReadyBefore=00000001 contextReadyAfter=00000001 contextBytesBefore=00001000 contextBytesAfter=00000FE8 contextAdvanced=00000018 methodTable=00000001 gcHeapOwned=00000001 threadAttached=00000001 gcCountBefore=00000000 gcCountAfter=00000000 status=00000000 collectionRequestedByProbe=00000000'
$r12Returned='[C188R12R4R5R4R5R2R2R4R2R3R12_ENTRY] event=returned entry=52313245 input=0000000E computed=00000462 completion=52313244 return=00000462 expected=00000462 record=PASS collection=not_requested model=DisplaySettingsProbe'
$r12State='[C188R12R4R5R2R3_ENTRY_STATE] status=00000000 bytes=00000070 membership=00000001 flags=00000001 cooperativeState=00000001 preemptiveFrame=00000001 flsMatch=00000001 threadId=0000000000001234 observedThreadId=0000000000001234 stackReady=00000001 stackInBounds=00000001 modules=00000001 codeManager=00000001 currentThread=0000000000005678 palFlsThread=0000000000005678 frame=FFFFFFFFFFFFFFFF stackLow=0000000000100000 stackHigh=0000000000200000 stackCurrent=0000000000180000 result=PASS'
function Invoke-R12OrderingFixture([string]$Name,[string[]]$Lines) {
    $path=Join-Path $fixtureRoot ($Name+'.serial.log')
    $out=Join-Path $fixtureRoot ($Name+'.analysis.json')
    [System.IO.File]::WriteAllLines($path,$Lines,[System.Text.UTF8Encoding]::new($false))
    & pwsh -NoProfile -ExecutionPolicy Bypass -File $r12AnalyzerPath -SerialPath $path `
        -CallSiteMapPath $CallSiteMapPath -OutputPath $out | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "R12 ordering fixture analyzer failed to preserve $Name." }
    return (Get-Content -LiteralPath $out -Raw | ConvertFrom-Json)
}
$r12Valid=Invoke-R12OrderingFixture 'r12-allocation-before-return' @($r12Attempt,$r12Alloc,$r12Returned,$r12State)
Assert-R13 ($r12Valid.managedEntryReturned -and $r12Valid.allocationSucceeded -and
    [string]::IsNullOrWhiteSpace([string]$r12Valid.parseError) -and
    $r12Valid.classification -ceq 'MANAGED_SETTINGS_OBJECT_RETURNED_MISMATCH') 'R12 positive ordering regression accepts genuine allocation before method return'
$r12Late=Invoke-R12OrderingFixture 'r12-allocation-after-return' @($r12Attempt,$r12Returned,$r12Alloc,$r12State)
Assert-R13 ($r12Late.classification -ceq 'MANAGED_SETTINGS_OBJECT_INCOMPLETE' -and
    [string]$r12Late.parseError -ceq 'r12_managed_entry_return_contract_mismatch') 'R12 negative ordering regression rejects allocation after method return'
$r14FrontierAnalysis=Invoke-R13Fixture 'r14-last-observed-attachment-marker' @($r14AttachmentFixture -split "`r?`n")
Assert-R13 ($r14FrontierAnalysis.managedEntryAttempted -and -not $r14FrontierAnalysis.managedEntryReturned -and
    -not $r14FrontierAnalysis.allocationSucceeded -and -not $r14FrontierAnalysis.gcCollectionStarted -and
    $r14FrontierAnalysis.classification -ceq 'MANAGED_GC_ROOT_INCOMPLETE') 'R14 serial fixture contains a pre-call attempt marker but no body, allocation, or collection evidence after attach'
$qacCensusFixture=Get-Content -Raw -LiteralPath (Join-Path $r15FixtureRoot 'r14-qac-transient-census.json') | ConvertFrom-Json
Assert-R13 ($qacCensusFixture.observations.Count -eq 3 -and
    -not $qacCensusFixture.observations[0].censusComplete -and $qacCensusFixture.observations[0].degraded -and
    $qacCensusFixture.observations[1].censusComplete -and -not $qacCensusFixture.observations[1].degraded -and
    $qacCensusFixture.observations[2].phase -ceq 'after-001' -and
    $qacCensusFixture.observations[2].qemuProcessCount -eq 0 -and $qacCensusFixture.observations[2].censusComplete -and
    $runner.Contains('$qacSnapshotRecords') -and $runner.Contains('$qacAfterWindowsCensusPath')) 'QAC transient in-run census warning and verified final clear census remain separate observations'

$report=[ordered]@{
    schema='C188R13_MANAGED_GC_ROOT_HOSTED_TESTS_V1'
    result='PASS'
    assertionCount=$checks.Count
    assertions=@($checks.ToArray())
    runtimeSourceCommit=[string]$manifest.runtimeIdentity.sourceCommit
    initialResult=1122
    expectedResult=1123
    managedCollectionRequested=$true
    collectionExecuted=$false
    hostedEvidenceOnly=$true
    qemuLaunched=$false
    qacReserved=$false
}
[System.IO.File]::WriteAllText((Join-Path $OutputDirectory 'hosted-test-results.json'),
    ($report | ConvertTo-Json -Depth 8),[System.Text.UTF8Encoding]::new($false))
Write-Output ("R13 managed-GC hosted tests: PASS assertions={0}; no native collection, guest boot, or QAC reservation." -f $checks.Count)
