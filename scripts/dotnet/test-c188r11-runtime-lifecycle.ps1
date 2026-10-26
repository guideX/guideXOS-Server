$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') {
    throw 'C188R11 runtime lifecycle tests require PowerShell Core.'
}
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$candidateBuild = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'build-c188-nativeaot-candidate.ps1') -Raw
$productionBuild = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'build-c188-production-nativeaot.ps1') -Raw
$managedBuild = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'build-managed-hostlog-proof.ps1') -Raw
$runtime = Get-Content -LiteralPath (Join-Path $repo 'tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp') -Raw
$script:caseCount = 0

function Assert-Lifecycle([string]$Name, [bool]$Condition) {
    if (-not $Condition) { throw "C188R11 runtime lifecycle test failed: $Name" }
    $script:caseCount++
}

Assert-Lifecycle 'diagnostic candidate selects persistent composite lifecycle' (
    [regex]::IsMatch($candidateBuild, '(?m)^\s*PersistentCompositeLifecycle\s*=\s*\$true\s*$'))
Assert-Lifecycle 'proof-disabled production candidate selects persistent composite lifecycle' (
    [regex]::IsMatch($productionBuild, '(?m)^\s*PersistentCompositeLifecycle\s*=\s*\$true\s*$'))
Assert-Lifecycle 'shared NativeAOT builder forwards the lifecycle option' (
    $managedBuild.Contains('if ($PersistentCompositeLifecycle) { $runtimePackBuildArguments += "-PersistentCompositeLifecycle" }'))

$persistentBranch = [regex]::Match($runtime,
    '(?s)#elif defined\(GUIDEXOS_NATIVEAOT_PERSISTENT_COMPOSITE_LIFECYCLE\)\s+if \(!g_persistentManagedHeapInitialized\) \{(?<initialize>.*?)\} else \{(?<preserve>.*?)\n\s*\}')
Assert-Lifecycle 'persistent lifecycle initializes and clears the arena only once' (
    $persistentBranch.Success -and
    $persistentBranch.Groups['initialize'].Value.Contains('g_guideXosManagedHeap[i] = 0;') -and
    $persistentBranch.Groups['initialize'].Value.Contains('g_persistentManagedHeapInitialized = true;'))
Assert-Lifecycle 'later reverse entries preserve the allocation cursor without clearing heap storage' (
    $persistentBranch.Success -and
    $persistentBranch.Groups['preserve'].Value.Contains('action=preserve') -and
    -not $persistentBranch.Groups['preserve'].Value.Contains('g_guideXosManagedHeap[i] = 0;'))

$reverseStart = $runtime.IndexOf('extern "C" __declspec(noinline) void __cdecl RhpReversePInvoke(void* frame)')
$reverseEnd = $runtime.IndexOf('extern "C" __declspec(noinline) void __cdecl RhpReversePInvokeReturn', $reverseStart)
$reverseBody = if ($reverseStart -ge 0 -and $reverseEnd -gt $reverseStart) {
    $runtime.Substring($reverseStart, $reverseEnd - $reverseStart)
} else { '' }
Assert-Lifecycle 'every managed reverse-P/Invoke reuses the selected runtime lifecycle policy' (
    $reverseBody.Contains('initializeRuntimeState(block);'))
Assert-Lifecycle 'both C188 builders reject a runtime manifest without persistent lifecycle' (
    $candidateBuild.Contains('runtimePackManifest.persistentCompositeLifecycle') -and
    $productionBuild.Contains('runtimePackManifest.persistentCompositeLifecycle'))

Write-Output "C188R11-RUNTIME-LIFECYCLE cases=$script:caseCount result=PASS"
