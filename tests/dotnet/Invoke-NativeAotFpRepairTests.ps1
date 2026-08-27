[CmdletBinding()]
param(
    [string]$RepoRoot = "",
    [string]$SourceCheckoutRoot = "",
    [string]$ResultPath = "",
    [switch]$KeepFixtures
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($RepoRoot)) { $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\.." )).Path }
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
$runtimePackRoot = Join-Path $RepoRoot "tools\dotnet\runtime-pack"
$helperPath = Join-Path $runtimePackRoot "apply-nativeaot-fp-repair.ps1"
$lockPath = Join-Path $runtimePackRoot "runtime-pack.lock.json"
$patchPath = Join-Path $runtimePackRoot "patches\nativeaot-amd64-fp-handoff.patch"
$lock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
$lockedCommit = ([string]$lock.nativeAotFpRepair.sourceCommit).Trim().ToLowerInvariant()
$targetRelativePaths = @(
    "src\coreclr\nativeaot\Runtime\StackFrameIterator.cpp",
    "src\coreclr\nativeaot\Runtime\windows\CoffNativeCodeManager.cpp"
)

if ([string]::IsNullOrWhiteSpace($SourceCheckoutRoot)) {
    $SourceCheckoutRoot = Join-Path $RepoRoot "out\dotnet\gc-feasibility-baseline\nativeaot-runtime"
}
$SourceCheckoutRoot = [System.IO.Path]::GetFullPath($SourceCheckoutRoot)
if (-not (Test-Path -LiteralPath $helperPath -PathType Leaf)) { throw "FP repair helper is missing: $helperPath" }
if (-not (Test-Path -LiteralPath $SourceCheckoutRoot -PathType Container)) { throw "Locked NativeAOT source checkout is missing: $SourceCheckoutRoot" }
if (-not (Test-Path -LiteralPath (Join-Path $SourceCheckoutRoot ".git"))) { throw "Source checkout is not a Git worktree: $SourceCheckoutRoot" }

function Get-Hash([string]$Path) {
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha256.ComputeHash([System.IO.File]::ReadAllBytes($Path))) -replace '-', '').ToUpperInvariant() }
    finally { $sha256.Dispose() }
}

function New-Fixture {
    param([string]$FixturePath)
    New-Item -ItemType Directory -Force -Path $FixturePath | Out-Null
    $archivePath = Join-Path $FixturePath "locked-source.tar"
    & git -C $SourceCheckoutRoot archive --format=tar --output="$archivePath" $lockedCommit `
        "src/coreclr/nativeaot/Runtime" "src/coreclr/gc" "src/coreclr/inc" "src/coreclr/vm" `
        "src/coreclr/pal/inc/rt" "src/coreclr/pal/src/include" "src/native/minipal"
    if ($LASTEXITCODE -ne 0) { throw "Unable to archive locked NativeAOT source fixture." }
    & tar.exe -xf $archivePath -C $FixturePath
    if ($LASTEXITCODE -ne 0) { throw "Unable to extract locked NativeAOT source fixture." }
    Set-Content -LiteralPath (Join-Path $FixturePath ".guidexos-runtime-source-commit") -Value $lockedCommit -Encoding ASCII
    return $FixturePath
}

function Invoke-Repair {
    param(
        [string]$FixturePath,
        [string]$InvocationResultPath,
        [string]$Commit = $lockedCommit,
        [string]$Patch = $patchPath
    )
    $arguments = @{
        SourceRoot = $FixturePath
        RuntimeCommit = $Commit
        LockPath = $lockPath
        PatchPath = $Patch
        ResultPath = $InvocationResultPath
    }
    $oldPreference = $ErrorActionPreference
    try {
        # Run the helper in-process.  This keeps the fixture harness portable
        # across Windows PowerShell installations whose child module probing
        # may omit Microsoft.PowerShell.Utility, while still treating the
        # helper's terminating failure as an explicit test result.
        $ErrorActionPreference = "Continue"
        try {
            $captured = @(& $helperPath @arguments 2>&1 6>&1)
            $output = ($captured | Out-String).Trim()
            $exitCode = 0
        }
        catch {
            $output = ($_ | Out-String).Trim()
            $exitCode = 1
        }
    }
    finally {
        $ErrorActionPreference = $oldPreference
    }
    [pscustomobject]@{
        ExitCode = $exitCode
        Output = $output
        ResultPath = $InvocationResultPath
    }
}

function Add-TestResult {
    param(
        [System.Collections.Generic.List[object]]$Results,
        [string]$Name,
        [bool]$Passed,
        [string]$Detail,
        [object]$Invocation = $null
    )
    $Results.Add([ordered]@{
        name = $Name
        result = if ($Passed) { "PASS" } else { "FAIL" }
        detail = $Detail
        exitCode = if ($null -eq $Invocation) { $null } else { $Invocation.ExitCode }
        output = if ($null -eq $Invocation) { $null } else { $Invocation.Output }
    }) | Out-Null
    Write-Host ("[C51 fixture] {0}: {1}" -f $Name, $(if ($Passed) { "PASS" } else { "FAIL" }))
    if (-not $Passed) { Write-Host ("  " + $Detail) -ForegroundColor Red }
}

$fixtureId = [guid]::NewGuid().ToString("N")
$fixtureRoot = Join-Path $RepoRoot "out\dotnet\c51-fp-fixture-tests-$fixtureId"
if ([string]::IsNullOrWhiteSpace($ResultPath)) {
    $ResultPath = Join-Path (Split-Path -Parent $fixtureRoot) "c51-fp-repair-tests-$fixtureId.json"
}
$ResultPath = [System.IO.Path]::GetFullPath($ResultPath)
$results = [System.Collections.Generic.List[object]]::new()
$testFailure = $false

try {
    & git -C $SourceCheckoutRoot cat-file -e "$lockedCommit`^{commit}"
    if ($LASTEXITCODE -ne 0) { throw "Locked source commit is not available in the source checkout: $lockedCommit" }

    New-Fixture $fixtureRoot | Out-Null
    $pristineResult = Join-Path $fixtureRoot "pristine.result.json"
    $pristine = Invoke-Repair $fixtureRoot $pristineResult
    $pristineJson = if (Test-Path -LiteralPath $pristineResult) { Get-Content -LiteralPath $pristineResult -Raw | ConvertFrom-Json } else { $null }
    $pristinePass = $pristine.ExitCode -eq 0 -and
        $pristine.Output -match 'state=PRISTINE_EXPECTED action=APPLIED' -and
        $null -ne $pristineJson -and $pristineJson.stateAfter -eq "PATCHED_CORRECTLY"
    Add-TestResult $results "pristine applies exactly once" $pristinePass "Expected PRISTINE_EXPECTED -> PATCHED_CORRECTLY." $pristine
    $testFailure = $testFailure -or -not $pristinePass

    $beforeSecondStack = Get-Hash (Join-Path $fixtureRoot $targetRelativePaths[0])
    $beforeSecondCoff = Get-Hash (Join-Path $fixtureRoot $targetRelativePaths[1])
    $alreadyResult = Join-Path $fixtureRoot "already.result.json"
    $already = Invoke-Repair $fixtureRoot $alreadyResult
    $afterSecondStack = Get-Hash (Join-Path $fixtureRoot $targetRelativePaths[0])
    $afterSecondCoff = Get-Hash (Join-Path $fixtureRoot $targetRelativePaths[1])
    $alreadyJson = if (Test-Path -LiteralPath $alreadyResult) { Get-Content -LiteralPath $alreadyResult -Raw | ConvertFrom-Json } else { $null }
    $alreadyPass = $already.ExitCode -eq 0 -and
        $already.Output -match 'state=ALREADY_PATCHED_CORRECTLY action=NONE' -and
        $null -ne $alreadyJson -and $alreadyJson.action -eq "NONE" -and
        $beforeSecondStack -eq $afterSecondStack -and $beforeSecondCoff -eq $afterSecondCoff
    Add-TestResult $results "already-patched rerun is a no-op" $alreadyPass "Expected ALREADY_PATCHED_CORRECTLY with byte-identical targets." $already
    $testFailure = $testFailure -or -not $alreadyPass

    $driftRoot = Join-Path $fixtureRoot "drift"
    New-Fixture $driftRoot | Out-Null
    $driftStack = Join-Path $driftRoot $targetRelativePaths[0]
    $driftText = Get-Content -LiteralPath $driftStack -Raw
    $driftNeedle = "void StackFrameIterator::Next()"
    if (-not $driftText.Contains($driftNeedle)) { throw "Drift fixture needle was not found in the locked StackFrameIterator source." }
    Set-Content -LiteralPath $driftStack -Value $driftText.Replace($driftNeedle, "void StackFrameIterator::NextC51Drift()") -Encoding ASCII
    $drift = Invoke-Repair $driftRoot (Join-Path $driftRoot "drift.result.json")
    $driftAfterText = Get-Content -LiteralPath $driftStack -Raw
    $driftPass = $drift.ExitCode -ne 0 -and $drift.Output -match 'state=FAIL category=SOURCE_DRIFT' -and
        $drift.Output -notmatch 'action=APPLIED' -and
        $driftAfterText.Contains("void StackFrameIterator::NextC51Drift()") -and
        -not $driftAfterText.Contains("m_FramePointer = (PTR_VOID)m_RegDisplay.GetFP();")
    Add-TestResult $results "source drift fails closed" $driftPass "Expected SOURCE_DRIFT and no postimage." $drift
    $testFailure = $testFailure -or -not $driftPass

    $partialRoot = Join-Path $fixtureRoot "partial"
    New-Fixture $partialRoot | Out-Null
    $partialPatch = Join-Path $partialRoot "partial.patch"
    $patchChunks = [regex]::Split((Get-Content -LiteralPath $patchPath -Raw), '(?m)(?=^diff --git )') | Where-Object { $_.Trim().Length -gt 0 }
    Set-Content -LiteralPath $partialPatch -Value ($patchChunks[0].TrimEnd() + [Environment]::NewLine) -Encoding ASCII
    $repoPrefix = $RepoRoot.TrimEnd('\', '/')
    $partialRelativeRoot = $partialRoot.Substring($repoPrefix.Length).TrimStart('\', '/').Replace('\', '/')
    & git -C $RepoRoot apply --unidiff-zero --whitespace=nowarn --directory=$partialRelativeRoot $partialPatch
    if ($LASTEXITCODE -ne 0) { throw "Unable to create partial-application fixture." }
    $partial = Invoke-Repair $partialRoot (Join-Path $partialRoot "partial.result.json")
    $partialPass = $partial.ExitCode -ne 0 -and $partial.Output -match 'state=FAIL category=PARTIAL_APPLICATION'
    Add-TestResult $results "partial application fails closed" $partialPass "Expected PARTIAL_APPLICATION with no second patch attempt." $partial
    $testFailure = $testFailure -or -not $partialPass

    $wrongRevisionRoot = Join-Path $fixtureRoot "wrong-revision"
    New-Fixture $wrongRevisionRoot | Out-Null
    $wrongRevision = Invoke-Repair $wrongRevisionRoot (Join-Path $wrongRevisionRoot "wrong-revision.result.json") ("0" * 40)
    $wrongRevisionPass = $wrongRevision.ExitCode -ne 0 -and $wrongRevision.Output -match 'state=FAIL category=WRONG_RUNTIME_IDENTITY'
    Add-TestResult $results "wrong runtime revision fails closed" $wrongRevisionPass "Expected WRONG_RUNTIME_IDENTITY before source mutation." $wrongRevision
    $testFailure = $testFailure -or -not $wrongRevisionPass

    $missingRoot = Join-Path $fixtureRoot "missing-target"
    New-Fixture $missingRoot | Out-Null
    Remove-Item -LiteralPath (Join-Path $missingRoot $targetRelativePaths[1]) -Force
    $missing = Invoke-Repair $missingRoot (Join-Path $missingRoot "missing.result.json")
    $missingPass = $missing.ExitCode -ne 0 -and $missing.Output -match 'state=FAIL category=MISSING_TARGET'
    Add-TestResult $results "missing target fails closed" $missingPass "Expected MISSING_TARGET before patch application." $missing
    $testFailure = $testFailure -or -not $missingPass

    $patchIdentityRoot = Join-Path $fixtureRoot "patch-identity"
    New-Fixture $patchIdentityRoot | Out-Null
    $modifiedPatch = Join-Path $patchIdentityRoot "modified.patch"
    Copy-Item -LiteralPath $patchPath -Destination $modifiedPatch -Force
    Add-Content -LiteralPath $modifiedPatch -Value "# C51 intentional fixture mutation"
    $patchIdentity = Invoke-Repair $patchIdentityRoot (Join-Path $patchIdentityRoot "patch-identity.result.json") $lockedCommit $modifiedPatch
    $patchIdentityPass = $patchIdentity.ExitCode -ne 0 -and $patchIdentity.Output -match 'state=FAIL category=PATCH_IDENTITY'
    Add-TestResult $results "patch identity mismatch fails closed" $patchIdentityPass "Expected PATCH_IDENTITY before source mutation." $patchIdentity
    $testFailure = $testFailure -or -not $patchIdentityPass
}
catch {
    $testFailure = $true
    Add-TestResult $results "fixture test harness" $false $_.Exception.Message
}
finally {
    $summary = [ordered]@{
        schemaVersion = 1
        c51Identifier = "C011EC51"
        result = if ($testFailure) { "FAIL" } else { "PASS" }
        lockedSourceCommit = $lockedCommit
        patchSha256 = Get-Hash $patchPath
        fixtureRoot = $fixtureRoot
        fixturesRetained = [bool]$KeepFixtures
        tests = @($results)
        passed = @($results | Where-Object { $_.result -eq "PASS" }).Count
        failed = @($results | Where-Object { $_.result -eq "FAIL" }).Count
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $ResultPath) | Out-Null
    $summary | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $ResultPath -Encoding ASCII
    Write-Host "[C51 fixture] result=$($summary.result) passed=$($summary.passed) failed=$($summary.failed) manifest=$ResultPath"
    if (-not $KeepFixtures -and (Test-Path -LiteralPath $fixtureRoot)) {
        Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
    }
}

if ($testFailure) { exit 1 }
exit 0
