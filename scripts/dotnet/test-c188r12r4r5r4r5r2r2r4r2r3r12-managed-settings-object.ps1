param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$RuntimePackManifest,
    [Parameter(Mandatory=$true)][string]$ManagedMap,
    [Parameter(Mandatory=$true)][string]$ManagedMainDisassembly,
    [Parameter(Mandatory=$true)][string]$ConstructorDisassembly,
    [Parameter(Mandatory=$true)][string]$CalculateDisassembly,
    [Parameter(Mandatory=$true)][string]$KernelBuildLog,
    [Parameter(Mandatory=$true)][string]$ProductionNativeAotMap,
    [Parameter(Mandatory=$true)][string]$R11HostedTestResults,
    [Parameter(Mandatory=$true)][string]$CallSiteMapPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [System.IO.Path]::GetFullPath($RepoRoot)
$inputs = @($RuntimePackManifest,$ManagedMap,$ManagedMainDisassembly,$ConstructorDisassembly,
    $CalculateDisassembly,$KernelBuildLog,$ProductionNativeAotMap,$R11HostedTestResults,$CallSiteMapPath)
foreach ($path in $inputs) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "R12 hosted-test input is missing: $path" }
}
$probePath = Join-Path $root 'samples\managed\HostLogProof\ManagedSettingsObjectProbe.cs'
$projectPath = Join-Path $root 'samples\managed\HostLogProof\HostLogProof.csproj'
$platformPath = Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_platform.cpp'
$allocationHeaderPath = Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_managed_settings_object_probe.h'
$entryHeaderPath = Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_managed_entry_probe.h'
$threadHeaderPath = Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_threadstore_readiness.h'
$kernelPath = Join-Path $root 'kernel\core\nativeaot_pal_qemu_test.cpp'
$runnerPath = Join-Path $root 'scripts\dotnet\run-c188r12r4r5-gc-ownership-qac-guest.ps1'
$analyzerPath = Join-Path $root 'scripts\dotnet\analyze-c188r12r4r5r4r5r2r2r4r2r3r12-managed-settings-object.ps1'
foreach ($path in @($probePath,$projectPath,$platformPath,$allocationHeaderPath,$entryHeaderPath,
        $threadHeaderPath,$kernelPath,$runnerPath,$analyzerPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "R12 source-contract input is missing: $path" }
}
$probe = Get-Content -LiteralPath $probePath -Raw
$project = Get-Content -LiteralPath $projectPath -Raw
$platform = Get-Content -LiteralPath $platformPath -Raw
$managedObjectAllocator = [regex]::Match($platform,
    '(?s)extern "C" __declspec\(noinline\) void\* __cdecl RhpNewFast\(void\* eeType\) \{(?<body>.*?)\n\}\n#endif\s*\n\s*#if defined\(GUIDEXOS_NATIVEAOT_C188R12R4_FACTORY_ATTRIBUTION\)').Groups['body'].Value
$allocationHeader = Get-Content -LiteralPath $allocationHeaderPath -Raw
$entryHeader = Get-Content -LiteralPath $entryHeaderPath -Raw
$threadHeader = Get-Content -LiteralPath $threadHeaderPath -Raw
$kernel = Get-Content -LiteralPath $kernelPath -Raw
$runner = Get-Content -LiteralPath $runnerPath -Raw
$analyzer = Get-Content -LiteralPath $analyzerPath -Raw
$map = Get-Content -LiteralPath $ManagedMap -Raw
$main = Get-Content -LiteralPath $ManagedMainDisassembly -Raw
$constructor = Get-Content -LiteralPath $ConstructorDisassembly -Raw
$calculate = Get-Content -LiteralPath $CalculateDisassembly -Raw
$kernelLog = Get-Content -LiteralPath $KernelBuildLog -Raw
$productionMap = Get-Content -LiteralPath $ProductionNativeAotMap -Raw
$manifest = Get-Content -LiteralPath $RuntimePackManifest -Raw | ConvertFrom-Json -ErrorAction Stop
$r11 = Get-Content -LiteralPath $R11HostedTestResults -Raw | ConvertFrom-Json -ErrorAction Stop
$checks = [System.Collections.Generic.List[object]]::new()
if (Test-Path -LiteralPath $OutputDirectory) { throw "Refusing to overwrite R12 hosted evidence: $OutputDirectory" }
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
function Assert-R12([bool]$Condition,[string]$Name) {
    $checks.Add([ordered]@{name=$Name;passed=$Condition})
    if (-not $Condition) { throw "R12 managed-settings hosted check failed: $Name" }
}
function Get-ManagedCodeSymbolAddress([string]$Symbol) {
    $pattern = '(?m)^\s*0001:[0-9A-Fa-f]{8}\s+' + [regex]::Escape($Symbol) +
        '\s+(?<address>[0-9A-Fa-f]{16})(?:\s|$)'
    $match = [regex]::Match($map,$pattern)
    if (-not $match.Success) { return $null }
    return [Convert]::ToUInt64($match.Groups['address'].Value,16).ToString('x')
}

$managedMainBody = [regex]::Match($probe,'(?s)public static int ManagedMain\(ManagedSettingsObjectProbeRecord\* record, uint seed\)\s*\{(?<body>.*?)\n    \}').Groups['body'].Value
$objectBody = [regex]::Match($probe,'(?s)public sealed class DisplaySettingsProbe\s*\{(?<body>.*?)\n\}').Groups['body'].Value
Assert-R12 ($probe -match '\[StructLayout\(LayoutKind\.Sequential, Pack = 4, Size = 16\)\]' -and
    $entryHeader -match 'sizeof\(GuideXosNativeAotManagedEntryProbeRecord\) == 16') 'R12 entry and native handoff records retain the fixed 16-byte ABI'
Assert-R12 ($probe -match '\[UnmanagedCallersOnly\(\s*EntryPoint = "ManagedMain",\s*CallConvs = new\[\] \{ typeof\(CallConvCdecl\) \}\)\]' -and
    $probe -match '\[MethodImpl\(MethodImplOptions\.NoInlining\)\]') 'R12 exports a distinct no-inline Cdecl managed entry'
Assert-R12 ($objectBody -match 'public int Width;\s*public int Height;\s*public int Scale;' -and
    $objectBody -match 'Width = width;\s*Height = height;\s*Scale = scale;' -and
    $objectBody -match '_constructedMarker = ConstructedMarker') 'DisplaySettingsProbe constructor initializes its fields and an execution witness'
Assert-R12 ($managedMainBody -match 'record->EntryMarker\s*=\s*EntryMarker' -and
    $managedMainBody -match 'new\(640, 480, 2\)' -and $managedMainBody -match 'settings\.Calculate\(\)' -and
    $managedMainBody -match 'record->Computed\s*=\s*unchecked\(\(uint\)result\)' -and
    $managedMainBody -match 'record->CompletionMarker\s*=\s*CompletionMarker' -and
    $managedMainBody -match 'GC\.KeepAlive\(settings\)' -and $probe -match 'Width \+ Height \+ Scale') 'ManagedMain constructs the object, reads its fields, records the result, and keeps it alive'
Assert-R12 ($managedMainBody -notmatch '\b(?:DllImport|LibraryImport|extern|stackalloc|throw|try|catch|Task|Thread)\b' -and
    $managedMainBody -notmatch 'GC\.Collect|RhpCollect|new\s*\[') 'R12 managed path avoids native substitutes, arrays, exceptions, threads, and explicit collection'
Assert-R12 ($project -match 'C188R12R4R5R2R3ManagedSettingsObjectProbe' -and
    $project -match '<EnableDefaultCompileItems>false</EnableDefaultCompileItems>' -and
    $project -match '<Compile Include="ManagedSettingsObjectProbe.cs" />' -and
    $project -match 'Compile Remove="ManagedSettingsObjectProbe.cs"') 'R12 source is confined to its explicit diagnostic build mode'
Assert-R12 ([bool]$manifest.c188r12r4r5r2r3ManagedSettingsObjectProbe -and
    [string]$manifest.identity -ceq 'guidexos-nativeaot-runtime-pack-amd64-c188r12r4r5r2r3-managed-settings-object-v1' -and
    [string]$manifest.runtimeIdentity.sourceCommit -ceq '9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3' -and
    [string]$manifest.runtimeIdentity.gc -ceq 'Workstation' -and
    -not [bool]$manifest.imageBackedArenaPresent -and [int64]$manifest.managedHeapBytes -eq 0) 'Runtime pack uses pinned real Workstation GC with no C188 arena'
Assert-R12 ([string]$manifest.c188r12r4r5r2r3ManagedSettingsObjectAllocationContract.helper -ceq 'RhpNewFast' -and
    [string]$manifest.c188r12r4r5r2r3ManagedSettingsObjectAllocationContract.stockHelper -ceq 'guideXosStockRhpNewFast' -and
    [bool]$manifest.c188r12r4r5r2r3ManagedSettingsObjectAllocationContract.implicitCollectionCountRecorded -and
    -not [bool]$manifest.c188r12r4r5r2r3ManagedSettingsObjectAllocationContract.collectionRequestedByManagedProbe) 'Allocation contract preserves the stock helper and observes implicit collections'
 $managedAllocationCallTarget = Get-ManagedCodeSymbolAddress 'RhpNewFast'
 $managedConstructorCallTarget = Get-ManagedCodeSymbolAddress 'HostLogProof_HostLogProof_DisplaySettingsProbe___ctor'
 $managedCalculateCallTarget = Get-ManagedCodeSymbolAddress 'HostLogProof_HostLogProof_DisplaySettingsProbe__Calculate'
Assert-R12 ($map -match 'ManagedSettingsObjectProbe.*ManagedMain' -and
    $null -ne $managedAllocationCallTarget -and $null -ne $managedConstructorCallTarget -and
    $null -ne $managedCalculateCallTarget -and $map -match 'guideXosStockRhpNewFast\s+[0-9A-Fa-f]{16}') 'NativeAOT map resolves entry, constructor, consumer, wrapper, and stock allocator'
Assert-R12 ($main -match '52313245' -and $main -match '52313244' -and
    $main -match ('call\s+0x' + $managedAllocationCallTarget + '\b') -and
    $main -match ('call\s+0x' + $managedConstructorCallTarget + '\b') -and
    $main -match ('call\s+0x' + $managedCalculateCallTarget + '\b')) 'Generated ManagedMain emits R12 markers and calls RhpNewFast, the constructor, and field consumer'
Assert-R12 ($constructor -match '52313243' -and $constructor -match '(?i)(?:mov|store)' -and
    $calculate -match '(?i)(?:add|lea)' -and $calculate -match '52313243') 'Generated constructor and consumer retain construction witness and field operations'
Assert-R12 ($managedObjectAllocator -match 'guideXosStockRhpNewFast\(eeType\)' -and
    [regex]::Matches($managedObjectAllocator,'guideXosStockRhpNewFast\(eeType\)').Count -eq 1 -and
    $managedObjectAllocator -match 'guidexos_nativeaot_gc_describe_object' -and
    $managedObjectAllocator -match 'gcReadBeforeSucceeded') 'Native wrapper calls the stock allocator once and validates the GC-owned object without a fallback'
Assert-R12 ($allocationHeader -match 'allocationContextBytesAdvanced' -and
    $allocationHeader -notmatch 'objectAddress|objectPointer|managedReference' -and
    $platform -match 'allocation\.objectOwnedByGcHeap' -and $platform -notmatch 'allocation\.objectAddress\s*=') 'Allocation side table stores numeric evidence without retaining the object reference'
Assert-R12 ($platform -match 'gcCountBefore' -and $platform -match 'gcCountAfter' -and
    $platform -match 'collectionRequestedByProbe=00000000' -and $analyzer -match 'implicitCollectionObserved') 'Diagnostics capture collection counts and never request collection'
Assert-R12 ($entryHeader -match 'sizeof\(GuideXosNativeAotManagedEntryProbeRuntimeState\) == 112' -and
    $threadHeader -match 'TelemetryFrame' -and $threadHeader -notmatch 'runtimeCell|m_pTransitionFrame\s*=') 'Post-call runtime state and telemetry side table preserve NativeAOT Thread fields'
Assert-R12 ($allocationHeader -match 'kGuideXosManagedSettingsObjectEntryMarker = 0x52313245u' -and
    $kernel -match 'kGuideXosManagedSettingsObjectResult' -and
    $kernel -match 'R12_ENTRY.*event=returned' -and $kernel -match 'C188R12R4R5R2R3_ENTRY_STATE' -and
    $kernel -match 'transitionStatus != 0u') 'Kernel validates managed markers and runtime state after the conditional transition gate'
Assert-R12 ($kernelLog -match '-DGXOS_NATIVEAOT_C188R12R4R5R2R3_MANAGED_SETTINGS_OBJECT_PROBE' -and
    $runner -match 'CreateNew' -and $runner -match 'qac' -and $analyzer -match 'R12_ALLOC') 'Candidate kernel, one-boot runner guard, and R12 analyzer are linked into the qualification path'
Assert-R12 (-not $productionMap.Contains('DisplaySettingsProbe') -and
    -not $productionMap.Contains('ManagedSettingsObjectProbe') -and
    -not $productionMap.Contains('MANAGED_SETTINGS_OBJECT_PROBE')) 'Production NativeAOT output excludes the diagnostic Settings model'
Assert-R12 ($r11.result -ceq 'PASS' -and [bool]$r11.qemuLaunched -eq $false -and
    [bool]$r11.qacReserved -eq $false -and [bool]$r11.managedCollectionRequested -eq $false) 'R11 managed-entry regression hosted suite passed without guest or QAC use'
Assert-R12 ($manifest.c188r12r4r5r2r3ManagedEntryProbeContract.methodAllocationFree -eq $false -and
    [int]$manifest.c188r12r4r5r2r3ManagedEntryProbeContract.expectedResult -eq 1122 -and
    [bool]$manifest.c188r12r4r5r2r3ManagedEntryProbeContract.collectionRequested -eq $false) 'R12 manifest distinguishes real allocation from the allocation-free R11 workload'

$analyzerFixtureRoot = Join-Path ([System.IO.Path]::GetFullPath($OutputDirectory)) 'analyzer-fixtures'
New-Item -ItemType Directory -Force -Path $analyzerFixtureRoot | Out-Null
$validMarkerFixturePath = Join-Path $analyzerFixtureRoot 'r12-markers-without-transition.serial.log'
$validMarkerFixture = @(
    '[C188R12R4R5R4R5R2R2R4R2R3R12_ENTRY] event=attempt abi=UnmanagedCallersOnly-Cdecl seed=0000000E symbol=ManagedMain model=DisplaySettingsProbe collection=not_requested',
    '[C188R12R4R5R4R5R2R2R4R2R3R12_ALLOC] helper=RhpNewFast stock=guideXosStockRhpNewFast requests=00000001 completed=00000001 objectSize=00000018 contextReadyBefore=00000001 contextReadyAfter=00000001 contextBytesBefore=00001000 contextBytesAfter=00000FE8 contextAdvanced=00000018 methodTable=00000001 gcHeapOwned=00000001 threadAttached=00000001 gcCountBefore=00000000 gcCountAfter=00000000 status=00000000 collectionRequestedByProbe=00000000',
    '[C188R12R4R5R4R5R2R2R4R2R3R12_ENTRY] event=returned entry=52313245 input=0000000E computed=00000462 completion=52313244 return=00000462 expected=00000462 record=PASS collection=not_requested model=DisplaySettingsProbe',
    '[C188R12R4R5R2R3_ENTRY_STATE] status=00000000 bytes=00000070 membership=00000001 flags=00000001 cooperativeState=00000001 preemptiveFrame=00000001 flsMatch=00000001 threadId=0000000000001234 observedThreadId=0000000000001234 stackReady=00000001 stackInBounds=00000001 modules=00000001 codeManager=00000001 currentThread=0000000000005678 palFlsThread=0000000000005678 frame=FFFFFFFFFFFFFFFF stackLow=0000000000100000 stackHigh=0000000000200000 stackCurrent=0000000000180000 result=PASS'
)
[System.IO.File]::WriteAllLines($validMarkerFixturePath,[string[]]$validMarkerFixture,[System.Text.UTF8Encoding]::new($false))
$validMarkerAnalysisPath = Join-Path $analyzerFixtureRoot 'r12-markers-without-transition.analysis.json'
& pwsh -NoProfile -ExecutionPolicy Bypass -File $analyzerPath -SerialPath $validMarkerFixturePath `
    -CallSiteMapPath $CallSiteMapPath -OutputPath $validMarkerAnalysisPath | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'R12 analyzer failed to analyze the marker-only hosted fixture.' }
$validMarkerAnalysis = Get-Content -LiteralPath $validMarkerAnalysisPath -Raw | ConvertFrom-Json
Assert-R12 ($validMarkerAnalysis.managedEntryReturned -and $validMarkerAnalysis.allocationSucceeded -and
    $validMarkerAnalysis.runtimeStateReady -and $validMarkerAnalysis.classification -ceq 'MANAGED_SETTINGS_OBJECT_RETURNED_MISMATCH' -and
    [string]::IsNullOrWhiteSpace([string]$validMarkerAnalysis.parseError) -and
    -not $validMarkerAnalysis.qemuLaunched -and -not $validMarkerAnalysis.qacReserved -and
    -not $validMarkerAnalysis.managedCollectionRequested) 'R12 analyzer parses markers without promoting a synthetic log to guest qualification'

$reorderedFixturePath = Join-Path $analyzerFixtureRoot 'allocation-after-return.serial.log'
$reorderedFixture = @($validMarkerFixture[0],$validMarkerFixture[2],$validMarkerFixture[1],$validMarkerFixture[3])
[System.IO.File]::WriteAllLines($reorderedFixturePath,[string[]]$reorderedFixture,[System.Text.UTF8Encoding]::new($false))
$reorderedAnalysisPath = Join-Path $analyzerFixtureRoot 'allocation-after-return.analysis.json'
& pwsh -NoProfile -ExecutionPolicy Bypass -File $analyzerPath -SerialPath $reorderedFixturePath `
    -CallSiteMapPath $CallSiteMapPath -OutputPath $reorderedAnalysisPath | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'R12 analyzer failed to analyze the reordered allocation fixture.' }
$reorderedAnalysis = Get-Content -LiteralPath $reorderedAnalysisPath -Raw | ConvertFrom-Json
Assert-R12 ($reorderedAnalysis.classification -ceq 'MANAGED_SETTINGS_OBJECT_INCOMPLETE' -and
    [string]$reorderedAnalysis.parseError -ceq 'r12_managed_entry_return_contract_mismatch') 'R12 analyzer accepts allocation-before-return and rejects allocation-after-return ordering'

$duplicateFixturePath = Join-Path $analyzerFixtureRoot 'duplicate-allocation.serial.log'
[System.IO.File]::WriteAllLines($duplicateFixturePath,[string[]]($validMarkerFixture + $validMarkerFixture[2]),[System.Text.UTF8Encoding]::new($false))
$duplicateAnalysisPath = Join-Path $analyzerFixtureRoot 'duplicate-allocation.analysis.json'
& pwsh -NoProfile -ExecutionPolicy Bypass -File $analyzerPath -SerialPath $duplicateFixturePath `
    -CallSiteMapPath $CallSiteMapPath -OutputPath $duplicateAnalysisPath | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'R12 analyzer failed to preserve a duplicate-marker fixture analysis.' }
$duplicateAnalysis = Get-Content -LiteralPath $duplicateAnalysisPath -Raw | ConvertFrom-Json
Assert-R12 ($duplicateAnalysis.classification -ceq 'MANAGED_SETTINGS_OBJECT_INCOMPLETE' -and
    [string]$duplicateAnalysis.parseError -ceq 'duplicate_r12_probe_marker' -and
    -not $duplicateAnalysis.qemuLaunched -and -not $duplicateAnalysis.qacReserved) 'R12 analyzer rejects duplicate allocation evidence without guest attribution'

$report = [ordered]@{
    schema='C188R12R4R5R4R5R2R2R4R2R3R12_MANAGED_SETTINGS_OBJECT_HOSTED_TESTS_V1'
    result='PASS'
    assertionCount=$checks.Count
    assertions=@($checks.ToArray())
    runtimeSourceCommit=[string]$manifest.runtimeIdentity.sourceCommit
    expectedResult=1122
    realManagedAllocation=$true
    hostedEvidenceOnly=$true
    qemuLaunched=$false
    qacReserved=$false
    managedCollectionRequested=$false
}
[System.IO.File]::WriteAllText((Join-Path $OutputDirectory 'hosted-test-results.json'),
    ($report | ConvertTo-Json -Depth 8),[System.Text.UTF8Encoding]::new($false))
Write-Output ("R12 managed-settings hosted tests: PASS assertions={0}; no guest boot or QAC reservation." -f $checks.Count)
