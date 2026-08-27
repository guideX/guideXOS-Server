[CmdletBinding()]
param(
    [ValidateSet("A", "B", "C", "D", "All")]
    [string]$Tier = "All",
    [string]$RepoRoot = "",
    [string]$EvidenceRoot = "",
    [string]$StockRuntimePackRoot = "",
    [string]$ExternalRuntimeRoot = "",
    [int]$FreshBootCount = 3,
    [int]$BootTimeoutSeconds = 120,
    [int]$CommandTimeoutSeconds = 1800
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
if ($FreshBootCount -ne 3) { throw "C51 reproducibility validation requires exactly three fresh boots." }
if ($BootTimeoutSeconds -lt 5) { throw "BootTimeoutSeconds must be at least 5." }
if ($CommandTimeoutSeconds -lt 30) { throw "CommandTimeoutSeconds must be at least 30." }

if ([string]::IsNullOrWhiteSpace($RepoRoot)) { $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\.." )).Path }
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path $RepoRoot "out\dotnet\c51-runtime-pack-validation"
}
$EvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceRoot)
$allowedEvidenceRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot "out\dotnet")).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $EvidenceRoot.StartsWith($allowedEvidenceRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "C51 evidence must remain under $allowedEvidenceRoot"
}

function Get-Hash([string]$Path) {
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha256.ComputeHash([System.IO.File]::ReadAllBytes($Path))) -replace '-', '').ToUpperInvariant() }
    finally { $sha256.Dispose() }
}

function Require-File([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "$Label is missing: $Path" }
}

function Invoke-BoundedPowerShell {
    param(
        [string]$ScriptPath,
        [string[]]$Arguments,
        [string]$LogPath,
        [int]$TimeoutSeconds
    )
    $powershell = (Get-Command powershell.exe -ErrorAction Stop).Source
    $allArguments = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $ScriptPath) + $Arguments
    $quotedArguments = @($allArguments | ForEach-Object {
        $value = [string]$_
        if ($value -match '[\s"]') { '"' + $value.Replace('"', '\"') + '"' } else { $value }
    })
    $errorPath = $LogPath + ".stderr.log"
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $LogPath) | Out-Null
    $process = Start-Process -FilePath $powershell -ArgumentList ($quotedArguments -join " ") `
        -WorkingDirectory $RepoRoot -RedirectStandardOutput $LogPath -RedirectStandardError $errorPath `
        -WindowStyle Hidden -PassThru
    $timedOut = -not $process.WaitForExit($TimeoutSeconds * 1000)
    if ($timedOut) {
        & taskkill.exe /PID $process.Id /T /F *> ($LogPath + ".taskkill.log")
        Wait-Process -Id $process.Id -Timeout 10 -ErrorAction SilentlyContinue
    }
    $process.Refresh()
    $exitCode = if ($timedOut) { 124 } else { [int]$process.ExitCode }
    return [pscustomobject]@{
        script = $ScriptPath
        command = $powershell + " " + ($quotedArguments -join " ")
        exitCode = $exitCode
        timedOut = [bool]$timedOut
        log = $LogPath
        stderr = $errorPath
        outputTail = if (Test-Path -LiteralPath $LogPath) { (Get-Content -LiteralPath $LogPath -Tail 40 | Out-String).Trim() } else { "" }
    }
}

function Test-PowerShellSyntax([string]$Path) {
    $tokens = $null
    $errors = $null
    [System.Management.Automation.Language.Parser]::ParseFile($Path, [ref]$tokens, [ref]$errors) | Out-Null
    if ($errors.Count -gt 0) {
        throw "PowerShell parse failed for ${Path}: " + (($errors | ForEach-Object { $_.Message }) -join "; ")
    }
}

function Assert-Equal([string]$Name, [object]$Actual, [object]$Expected) {
    if ([string]$Actual -ne [string]$Expected) { throw "$Name mismatch: expected '$Expected', got '$Actual'" }
}

function Assert-RuntimePackManifest {
    param([string]$ManifestPath, [string]$LockPath, [string]$PatchPath, [string]$ExpectedSourceCommit)
    Require-File $ManifestPath "C51 runtime-pack manifest"
    $manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
    Assert-Equal "runtime-pack c51Identifier" $manifest.c51Identifier "C011EC51"
    Assert-Equal "runtime-pack nativeAotFpRepair" $manifest.nativeAotFpRepair $true
    Assert-Equal "runtime-pack source commit" $manifest.nativeAotFpRepairSourceCommit $ExpectedSourceCommit
    Assert-Equal "runtime-pack external source commit" $manifest.externalRuntimeCommit $ExpectedSourceCommit
    Assert-Equal "runtime-pack patch state" $manifest.nativeAotFpRepairStateAfter "PATCHED_CORRECTLY"
    Assert-Equal "runtime-pack patch action" $manifest.nativeAotFpRepairAction "APPLIED"
    Assert-Equal "runtime-pack patch hash" $manifest.nativeAotFpRepairPatchSha256 (Get-Hash $PatchPath)
    Assert-Equal "runtime-pack lock hash" $manifest.lockFileSha256 (Get-Hash $LockPath)
    Assert-Equal "runtime-pack stale-artifact guard" $manifest.staleArtifactProtection.result "PASS"
    Assert-Equal "runtime-pack semantic guard" $manifest.semanticRewriteGuard.result "PASS"
    if ($manifest.semanticRewriteGuard.c46SemanticCompileDefine -ne $false -or
        $manifest.semanticRewriteGuard.c47SemanticCompileDefine -ne $false -or
        $manifest.semanticRewriteGuard.c48SemanticCompileDefine -ne $false -or
        $manifest.semanticRewriteGuard.generatedStackFrameIteratorReplacement -ne $false) {
        throw "C51 runtime-pack manifest semantic-rewrite guard is not false for all C46/C47/C48/generated-replacement fields."
    }
    Assert-Equal "runtime-pack source revision marker" (Get-Content -LiteralPath $manifest.nativeAotFpRepairSourceRevisionMarker -Raw).Trim() $ExpectedSourceCommit
    $targetFiles = @($manifest.nativeAotFpRepairSourceFiles.PSObject.Properties)
    if ($targetFiles.Count -ne 2) { throw "C51 runtime-pack manifest has $($targetFiles.Count) patched source files; expected 2." }
    foreach ($target in $targetFiles) {
        Require-File $target.Value.path "C51 patched source file"
        Assert-Equal "patched source hash $($target.Name)" (Get-Hash $target.Value.path) $target.Value.sha256
    }
    foreach ($objectPath in @(
        $manifest.nativeAotFpRepairObjects.stackFrameIterator,
        $manifest.nativeAotFpRepairObjects.coffNativeCodeManager
    )) { Require-File $objectPath "C51 patched runtime object" }
    Assert-Equal "StackFrameIterator object hash" (Get-Hash $manifest.nativeAotFpRepairObjects.stackFrameIterator) $manifest.nativeAotFpRepairObjects.stackFrameIteratorSha256
    Assert-Equal "CoffNativeCodeManager object hash" (Get-Hash $manifest.nativeAotFpRepairObjects.coffNativeCodeManager) $manifest.nativeAotFpRepairObjects.coffNativeCodeManagerSha256
    Require-File $manifest.adaptedRuntimeLibrary "C51 adapted Runtime.WorkstationGC archive"
    Assert-Equal "adapted archive hash" (Get-Hash $manifest.adaptedRuntimeLibrary) $manifest.adaptedRuntimeLibrarySha256
    Require-File $manifest.archiveMembership.memberList "C51 archive member list"
    foreach ($property in $manifest.archiveMembership.patchedMemberCounts.PSObject.Properties) { Assert-Equal "patched archive member $($property.Name) count" $property.Value 1 }
    foreach ($property in $manifest.archiveMembership.removedMemberCounts.PSObject.Properties) { Assert-Equal "removed archive member $($property.Name) count" $property.Value 0 }
    if ($manifest.archiveMembership.duplicatePatchedMembers -ne 0) { throw "C51 adapted archive contains duplicate patched members." }
    if ($manifest.buildCommandIdentity.freshOutputRoot -ne $true) { throw "C51 runtime-pack build did not prove a fresh output root." }
    return $manifest
}

function Get-LatestManifest([string]$Root, [string]$Name) {
    $file = Get-ChildItem -LiteralPath $Root -Recurse -File -Filter $Name -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    if ($null -eq $file) { throw "No $Name was produced under $Root" }
    return $file.FullName
}

$runtimePackRoot = Join-Path $RepoRoot "tools\dotnet\runtime-pack"
$lockPath = Join-Path $runtimePackRoot "runtime-pack.lock.json"
$patchPath = Join-Path $runtimePackRoot "patches\nativeaot-amd64-fp-handoff.patch"
$fixtureTestsScript = Join-Path $RepoRoot "tests\dotnet\Invoke-NativeAotFpRepairTests.ps1"
$buildScript = Join-Path $RepoRoot "scripts\dotnet\build-guidexos-nativeaot-runtime-pack.ps1"
$gcScript = Join-Path $RepoRoot "scripts\smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1"
$ordinaryScript = Join-Path $RepoRoot "scripts\dotnet\Validate-GuideXOSOrdinaryBoot.ps1"
$lock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
$lockedCommit = ([string]$lock.nativeAotFpRepair.sourceCommit).Trim().ToLowerInvariant()
$runId = "run-" + (Get-Date -Format "yyyyMMdd-HHmmssfff") + "-" + ([guid]::NewGuid().ToString("N").Substring(0, 8))
$runRoot = Join-Path $EvidenceRoot $runId
New-Item -ItemType Directory -Force -Path $runRoot | Out-Null
$manifestPath = Join-Path $runRoot "c51.validation.manifest.json"
$tierResults = [ordered]@{}
$failure = $null
$runtimePackManifestPath = $null
$gcManifestPath = $null
$ordinaryManifestPath = $null

$stageLimit = switch ($Tier) {
    "A" { 1 }
    "B" { 2 }
    "C" { 3 }
    default { 4 }
}

try {
    Require-File $lockPath "C51 runtime-pack lock"
    Require-File $patchPath "C51 runtime-pack patch"
    foreach ($scriptPath in @($fixtureTestsScript, $buildScript, $gcScript, $ordinaryScript)) { Require-File $scriptPath "C51 validation script" }

    $tierA = [ordered]@{ result = "FAIL"; checks = [ordered]@{} }
    $tierA.checks.lockIdentity = [ordered]@{
        architecture = $lock.architecture
        targetFramework = $lock.targetFramework
        runtimeIdentifier = $lock.runtimeIdentifier
        nativeAot = $lock.ilCompiler.version
        sourceCommit = $lockedCommit
        patchSha256 = Get-Hash $patchPath
        lockPatchSha256 = $lock.nativeAotFpRepair.patchSha256
    }
    Assert-Equal "C51 lock patch hash" $tierA.checks.lockIdentity.patchSha256 $tierA.checks.lockIdentity.lockPatchSha256
    if ([string]$lock.architecture -ne "amd64" -or [string]$lock.targetFramework -ne "net9.0" -or [string]$lock.runtimeIdentifier -ne "win-x64" -or [string]$lock.ilCompiler.version -ne "9.0.0") {
        throw "C51 lock identity is not AMD64/net9.0/win-x64 NativeAOT 9.0.0."
    }
    foreach ($scriptPath in @($fixtureTestsScript, $buildScript, $gcScript, $ordinaryScript, $PSCommandPath)) { Test-PowerShellSyntax $scriptPath }
    $gcText = Get-Content -LiteralPath $gcScript -Raw
    $buildText = Get-Content -LiteralPath (Join-Path $runtimePackRoot "build-runtime-pack.ps1") -Raw
    if ($gcText -notmatch '\$useC011EC46SemanticInjection\s*=\s*\$isC011EC46\s+-and\s+-not\s+\$isC011EC50Production' -or
        $gcText -notmatch 'C51 semantic-rewrite guard failed' -or
        $gcText -notmatch 'runtimePackBuildManifest\.nativeAotFpRepairStateAfter' -or
        $buildText -notmatch 'C51 stale-artifact protection' -or
        $buildText -notmatch 'archive membership validation') {
        throw "C51 static semantic/stale-artifact/archive validation guard is incomplete."
    }
    $tierA.checks.staticGuards = "PASS"
    $fixtureResultPath = Join-Path $runRoot "nativeaot-fp-repair-fixtures.json"
    $fixtureInvocation = Invoke-BoundedPowerShell $fixtureTestsScript @(
        "-RepoRoot", $RepoRoot,
        "-ResultPath", $fixtureResultPath,
        "-KeepFixtures"
    ) (Join-Path $runRoot "tier-a-fixtures.log") $CommandTimeoutSeconds
    $tierA.checks.fixtureInvocation = [ordered]@{
        exitCode = $fixtureInvocation.exitCode
        timedOut = $fixtureInvocation.timedOut
        log = $fixtureInvocation.log
        stderr = $fixtureInvocation.stderr
    }
    if ($fixtureInvocation.exitCode -ne 0 -or -not (Test-Path -LiteralPath $fixtureResultPath -PathType Leaf)) {
        throw "C51 fixture tests failed or did not produce a result manifest. exit=$($fixtureInvocation.exitCode) timedOut=$($fixtureInvocation.timedOut) resultPath=$fixtureResultPath"
    }
    $fixtureResult = Get-Content -LiteralPath $fixtureResultPath -Raw | ConvertFrom-Json
    if ($fixtureResult.result -ne "PASS" -or [int]$fixtureResult.failed -ne 0) { throw "C51 fixture test result is not PASS. result=$($fixtureResult.result) failed=$($fixtureResult.failed)" }
    $tierA.checks.fixtureResult = [ordered]@{
        path = $fixtureResultPath
        sha256 = Get-Hash $fixtureResultPath
        result = $fixtureResult.result
        passed = [int]$fixtureResult.passed
        failed = [int]$fixtureResult.failed
        tests = @($fixtureResult.tests | ForEach-Object { [ordered]@{ name = $_.name; result = $_.result; exitCode = $_.exitCode } })
    }
    $tierA.result = "PASS"
    $tierResults.A = $tierA
    Write-Host "[C51] tier=A PASS"

    if ($stageLimit -ge 2) {
        $buildOutputRoot = Join-Path $runRoot "runtime-pack"
        $buildArguments = @("-RepoRoot", $RepoRoot, "-OutputRoot", $buildOutputRoot, "-NativeAotFpRepair", "-Clean")
        if (-not [string]::IsNullOrWhiteSpace($StockRuntimePackRoot)) { $buildArguments += @("-StockRuntimePackRoot", [System.IO.Path]::GetFullPath($StockRuntimePackRoot)) }
        if (-not [string]::IsNullOrWhiteSpace($ExternalRuntimeRoot)) { $buildArguments += @("-ExternalRuntimeRoot", [System.IO.Path]::GetFullPath($ExternalRuntimeRoot)) }
        $tierB = [ordered]@{ result = "FAIL"; invocation = $null; runtimePackManifest = $null }
        $tierB.invocation = Invoke-BoundedPowerShell $buildScript $buildArguments (Join-Path $runRoot "tier-b-runtime-pack.log") $CommandTimeoutSeconds
        if ($tierB.invocation.exitCode -ne 0) { throw "C51 fresh runtime-pack build failed." }
        $runtimePackManifestPath = Join-Path $buildOutputRoot "runtime-pack.manifest.json"
        $runtimePackManifest = Assert-RuntimePackManifest $runtimePackManifestPath $lockPath $patchPath $lockedCommit
        $tierB.runtimePackManifest = [ordered]@{
            path = $runtimePackManifestPath
            sha256 = Get-Hash $runtimePackManifestPath
            sourceCommit = $runtimePackManifest.nativeAotFpRepairSourceCommit
            patchState = $runtimePackManifest.nativeAotFpRepairStateAfter
            archiveSha256 = $runtimePackManifest.adaptedRuntimeLibrarySha256
            stackObjectSha256 = $runtimePackManifest.nativeAotFpRepairObjects.stackFrameIteratorSha256
            coffObjectSha256 = $runtimePackManifest.nativeAotFpRepairObjects.coffNativeCodeManagerSha256
        }
        $tierB.result = "PASS"
        $tierResults.B = $tierB
        Write-Host "[C51] tier=B PASS manifest=$runtimePackManifestPath"
    }
    elseif ($stageLimit -lt 2) { $tierResults.B = [ordered]@{ result = "NOT_REQUESTED" } }

    if ($stageLimit -ge 3) {
        if ($null -eq $runtimePackManifestPath) { throw "C51 tier C requires a passing tier B runtime-pack manifest." }
        $tierC = [ordered]@{ result = "FAIL"; invocation = $null; gcManifest = $null }
        $gcEvidenceRoot = Join-Path $runRoot "gc-proof"
        $tierC.invocation = Invoke-BoundedPowerShell $gcScript @(
            "-RepoRoot", $RepoRoot,
            "-EvidenceRoot", $gcEvidenceRoot,
            "-TimeoutSeconds", $BootTimeoutSeconds,
            "-FreshBootCount", $FreshBootCount,
            "-RuntimePackManifest", $runtimePackManifestPath,
            "-ProofMode", "productionized-second-collection"
        ) (Join-Path $runRoot "tier-c-gc-proof.log") $CommandTimeoutSeconds
        if ($tierC.invocation.exitCode -ne 0) { throw "C51 productionized three-boot GC proof failed." }
        $gcManifestPath = Get-LatestManifest $gcEvidenceRoot "manifest.json"
        $gcManifest = Get-Content -LiteralPath $gcManifestPath -Raw | ConvertFrom-Json
        if ($gcManifest.marker -ne "C011EC50" -or $gcManifest.productionized -ne $true -or $gcManifest.semanticRewriteGuard.result -ne "PASS" -or
            [System.IO.Path]::GetFullPath($gcManifest.runtimePackBuildManifest) -ne [System.IO.Path]::GetFullPath($runtimePackManifestPath) -or
            $gcManifest.c49.allRunsComplete -ne $true -or $gcManifest.c49.semanticConsistent -ne $true -or
            $gcManifest.c49.condemnedGeneration -ne "0x00000001" -or $gcManifest.c49.collectionOrdinal -ne "0x00000002" -or
            $gcManifest.c49.plannerDecision -ne "0x00000001" -or $gcManifest.c49.compactBranch -ne "0x00000001" -or
            $gcManifest.c49.sweepBranch -ne "0x00000000" -or $gcManifest.c49.postGcAllocationCount -ne "0x00000008" -or
            $gcManifest.c49.invariantFailures -ne "0x00000000" -or $gcManifest.c49.sensitiveDiagnosticAllocations -ne "0x00000000") {
            throw "C51 C50 production manifest did not pass all retained semantic and three-run assertions."
        }
        if (@($gcManifest.qemu.runs).Count -ne 3) { throw "C51 C50 manifest does not contain exactly three fresh runs." }
        $tierC.gcManifest = [ordered]@{
            path = $gcManifestPath
            sha256 = Get-Hash $gcManifestPath
            marker = $gcManifest.marker
            productionized = $gcManifest.productionized
            outcome = $gcManifest.outcome
            successLevel = $gcManifest.successLevel
            freshRunCount = @($gcManifest.qemu.runs).Count
            serialSha256 = @($gcManifest.qemu.serialSha256)
        }
        $tierC.result = "PASS"
        $tierResults.C = $tierC
        Write-Host "[C51] tier=C PASS manifest=$gcManifestPath"
    }
    elseif ($stageLimit -lt 3) { $tierResults.C = [ordered]@{ result = "NOT_REQUESTED" } }

    if ($stageLimit -ge 4) {
        $tierD = [ordered]@{ result = "FAIL"; invocation = $null; ordinaryManifest = $null }
        $ordinaryEvidenceRoot = Join-Path $runRoot "ordinary-boot"
        $tierD.invocation = Invoke-BoundedPowerShell $ordinaryScript @(
            "-RepoRoot", $RepoRoot,
            "-EvidenceRoot", $ordinaryEvidenceRoot,
            "-TimeoutSeconds", $BootTimeoutSeconds,
            "-FreshBootCount", $FreshBootCount
        ) (Join-Path $runRoot "tier-d-ordinary-boot.log") $CommandTimeoutSeconds
        if ($tierD.invocation.exitCode -ne 0) { throw "C51 precise ordinary-boot validation failed." }
        $ordinaryManifestPath = Get-LatestManifest $ordinaryEvidenceRoot "ordinary-boot.manifest.json"
        $ordinaryManifest = Get-Content -LiteralPath $ordinaryManifestPath -Raw | ConvertFrom-Json
        if ($ordinaryManifest.outcome -ne "PASS" -or $ordinaryManifest.freshBootCount -ne 3 -or $ordinaryManifest.noCanonicalKernelMutation -ne $true) {
            throw "C51 ordinary-boot manifest did not pass its exact marker and restoration assertions."
        }
        $tierD.ordinaryManifest = [ordered]@{
            path = $ordinaryManifestPath
            sha256 = Get-Hash $ordinaryManifestPath
            outcome = $ordinaryManifest.outcome
            freshBootCount = $ordinaryManifest.freshBootCount
            noCanonicalKernelMutation = $ordinaryManifest.noCanonicalKernelMutation
            serialSha256 = @($ordinaryManifest.boots | ForEach-Object { $_.serialSha256 })
        }
        $tierD.result = "PASS"
        $tierResults.D = $tierD
        Write-Host "[C51] tier=D PASS manifest=$ordinaryManifestPath"
    }
    elseif ($stageLimit -lt 4) { $tierResults.D = [ordered]@{ result = "NOT_REQUESTED" } }
}
catch {
    $failure = $_.Exception.Message
    Write-Host "[C51] FAIL: $failure" -ForegroundColor Red
}

$requestedTierResults = @($tierResults.GetEnumerator() | Where-Object { $_.Value.result -ne "NOT_REQUESTED" })
$allRequestedPassed = $null -eq $failure -and $requestedTierResults.Count -eq $stageLimit -and
    @($requestedTierResults | Where-Object { $_.Value.result -ne "PASS" }).Count -eq 0
$successLevel = 0
if ($tierResults.Contains("A") -and $tierResults.A.result -eq "PASS") { $successLevel = 1 }
if ($tierResults.Contains("B") -and $tierResults.B.result -eq "PASS") { $successLevel = 2 }
if ($tierResults.Contains("C") -and $tierResults.C.result -eq "PASS") { $successLevel = 4 }
if ($tierResults.Contains("D") -and $tierResults.D.result -eq "PASS") { $successLevel = 5 }
$outcome = if ($allRequestedPassed -and $Tier -eq "All" -and $successLevel -eq 5) { "A" } elseif ($successLevel -ge 4) { "B" } elseif ($successLevel -ge 2) { "C" } else { "D" }
$topManifest = [ordered]@{
    schemaVersion = 1
    c51Identifier = "C011EC51"
    outcome = $outcome
    successLevel = $successLevel
    requestedTier = $Tier
    requestedFreshBootCount = $FreshBootCount
    bootTimeoutSeconds = $BootTimeoutSeconds
    commandTimeoutSeconds = $CommandTimeoutSeconds
    failure = $failure
    repository = [ordered]@{
        root = $RepoRoot
        head = (& git -C $RepoRoot rev-parse HEAD).Trim()
        branch = (& git -C $RepoRoot branch --show-current).Trim()
        status = @(& git -C $RepoRoot status --short)
        upstream = (& git -C $RepoRoot rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>$null).Trim()
    }
    runtimeIdentity = [ordered]@{
        nativeAot = $lock.ilCompiler.version
        architecture = "AMD64"
        gc = "Workstation"
        gcInterfaces = "5.3 / 2"
        targetFramework = $lock.targetFramework
        runtimeIdentifier = $lock.runtimeIdentifier
        sourceCommit = $lockedCommit
        lockSha256 = Get-Hash $lockPath
        patchSha256 = Get-Hash $patchPath
    }
    tierResults = $tierResults
    runtimePackManifest = if ($null -eq $runtimePackManifestPath -or -not (Test-Path -LiteralPath $runtimePackManifestPath -PathType Leaf)) { $null } else { [ordered]@{ path = $runtimePackManifestPath; sha256 = Get-Hash $runtimePackManifestPath } }
    gcProofManifest = if ($null -eq $gcManifestPath -or -not (Test-Path -LiteralPath $gcManifestPath -PathType Leaf)) { $null } else { [ordered]@{ path = $gcManifestPath; sha256 = Get-Hash $gcManifestPath } }
    ordinaryBootManifest = if ($null -eq $ordinaryManifestPath -or -not (Test-Path -LiteralPath $ordinaryManifestPath -PathType Leaf)) { $null } else { [ordered]@{ path = $ordinaryManifestPath; sha256 = Get-Hash $ordinaryManifestPath } }
    semanticRewriteGuard = [ordered]@{
        result = if ($allRequestedPassed) { "PASS" } else { "FAIL" }
        c46SemanticCompileDefine = $false
        c47SemanticCompileDefine = $false
        c48SemanticCompileDefine = $false
        generatedStackFrameIteratorReplacement = $false
        productionUsesDurablePatch = $true
    }
    c42 = [ordered]@{ included = $false; historicalAvailable = $true; rationale = "C011EC51 validates C50's second collection and ordinary boot; C42's third-collection lifecycle remains historical and is not enabled." }
    evidenceRoot = $runRoot
    manifestPath = $manifestPath
}
$topManifest | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
Write-Host "[C51] outcome=$outcome successLevel=$successLevel requestedTier=$Tier manifest=$manifestPath"
if (-not $allRequestedPassed) { exit 1 }
exit 0
