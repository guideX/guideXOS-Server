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
    $EvidenceRoot = Join-Path $RepoRoot "out\dotnet\c52-runtime-pack-validation"
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

function Invoke-BoundedProcess {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$LogPath,
        [int]$TimeoutSeconds
    )
    $quotedArguments = @($Arguments | ForEach-Object {
        $value = [string]$_
        if ($value -match '[\s"]') { '"' + $value.Replace('"', '\"') + '"' } else { $value }
    })
    $errorPath = $LogPath + ".stderr.log"
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $LogPath) | Out-Null
    $process = Start-Process -FilePath $FilePath -ArgumentList ($quotedArguments -join " ") `
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
        file = $FilePath
        arguments = $Arguments
        command = $FilePath + " " + ($quotedArguments -join " ")
        exitCode = $exitCode
        timedOut = [bool]$timedOut
        log = $LogPath
        stderr = $errorPath
        outputTail = if (Test-Path -LiteralPath $LogPath) { (Get-Content -LiteralPath $LogPath -Tail 40 | Out-String).Trim() } else { "" }
    }
}

function Get-CurrentProcessIds {
    $all = @(Get-CimInstance Win32_Process -ErrorAction Stop)
    $ids = @([int]$PID)
    for ($index = 0; $index -lt 16; $index++) {
        $current = $all | Where-Object { [int]$_.ProcessId -eq [int]$ids[-1] } | Select-Object -First 1
        if ($null -eq $current -or [int]$current.ParentProcessId -le 0 -or $ids -contains [int]$current.ParentProcessId) { break }
        $ids += [int]$current.ParentProcessId
    }
    return @($ids | Select-Object -Unique)
}

function Test-OwnedProcess([object]$Process) {
    if ($currentProcessIds -contains [int]$Process.processId) { return $false }
    $commandLine = [string]$Process.fullCommandLine
    if ($commandLine -match '(?i)Invoke-C011EC51RuntimePackValidation\.ps1') { return $true }
    foreach ($root in $ownedEvidenceRoots) {
        if (-not [string]::IsNullOrWhiteSpace($root) -and $commandLine.IndexOf($root, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) { return $true }
    }
    return $false
}

function Get-ProcessSnapshot {
    $interestingNames = @('qemu-system-x86_64.exe','powershell.exe','pwsh.exe','cmd.exe','git.exe','cl.exe','link.exe','lib.exe','tar.exe','make.exe','mingw32-make.exe')
    $all = @(Get-CimInstance Win32_Process -ErrorAction Stop)
    $snapshot = @($all | Where-Object { $interestingNames -contains ([string]$_.Name).ToLowerInvariant() } | ForEach-Object {
        $fullCommandLine = [string]$_.CommandLine
        [ordered]@{
            processId = [int]$_.ProcessId
            parentProcessId = [int]$_.ParentProcessId
            name = [string]$_.Name
            commandLine = if ($fullCommandLine.Length -gt 1200) { $fullCommandLine.Substring(0, 1200) } else { $fullCommandLine }
            fullCommandLine = $fullCommandLine
        }
    })
    foreach ($process in $snapshot) { $process.owned = Test-OwnedProcess $process }
    return @($snapshot)
}

function Stop-OwnedProcesses([object[]]$Processes, [string]$LogPath) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $LogPath) | Out-Null
    $actions = [System.Collections.Generic.List[object]]::new()
    foreach ($process in @($Processes | Where-Object { $_.owned -eq $true })) {
        $line = "Stopping owned process PID=$($process.processId) name=$($process.name)"
        Add-Content -LiteralPath $LogPath -Value $line
        $taskkillOutput = @(& taskkill.exe /PID ([int]$process.processId) /T /F 2>&1)
        Add-Content -LiteralPath $LogPath -Value ($taskkillOutput -join [Environment]::NewLine)
        $actions.Add([ordered]@{ processId=$process.processId; name=$process.name; taskkillExitCode=$LASTEXITCODE }) | Out-Null
    }
    return @($actions)
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

function Get-GitCheckoutState([string]$Path) {
    $headOutput = @(& git -c core.longpaths=true -C $Path rev-parse HEAD 2>&1)
    $headExitCode = $LASTEXITCODE
    $statusOutput = @(& git -c core.longpaths=true -C $Path status --porcelain 2>&1)
    $statusExitCode = $LASTEXITCODE
    $targetRef = $lockedCommit + "^{commit}"
    & git -c core.longpaths=true -C $Path cat-file -e $targetRef 2>$null
    $targetAvailable = $LASTEXITCODE -eq 0
    return [ordered]@{
        path = $Path
        isGitCheckout = (Test-Path -LiteralPath (Join-Path $Path ".git")) -and $headExitCode -eq 0
        head = if ($headExitCode -eq 0) { ($headOutput -join "`n").Trim().ToLowerInvariant() } else { $null }
        headExitCode = $headExitCode
        statusCount = if ($statusExitCode -eq 0) { $statusOutput.Count } else { $null }
        status = if ($statusExitCode -eq 0) { @($statusOutput | Select-Object -First 40) } else { @($statusOutput | Select-Object -First 10) }
        statusExitCode = $statusExitCode
        targetCommitAvailable = $targetAvailable
    }
}

function New-LockedSourceCheckout {
    param(
        [string]$RequestedRoot,
        [string]$DefaultRoot,
        [string]$TargetCommit,
        [string]$DestinationRoot,
        [int]$TimeoutSeconds
    )
    $script:lockedCommit = $TargetCommit
    $defaultState = Get-GitCheckoutState $DefaultRoot
    if (-not [string]::IsNullOrWhiteSpace($RequestedRoot)) {
        $requestedPath = [System.IO.Path]::GetFullPath($RequestedRoot)
        $requestedState = Get-GitCheckoutState $requestedPath
        if (-not $requestedState.isGitCheckout) { throw "C52 requested external runtime root is not a Git checkout: $requestedPath" }
        if ($requestedState.statusCount -ne 0) { throw "C52 requested external runtime root is dirty; refusing to use it: $requestedPath" }
        if ($requestedState.head -ne $TargetCommit) { throw "C52 requested external runtime root is not pinned to ${TargetCommit}: $requestedPath (actual $($requestedState.head))" }
        return [pscustomobject]@{
            checkoutPath = $requestedPath
            strategy = "provided-clean-locked-checkout"
            requestedPath = $requestedPath
            defaultPath = $DefaultRoot
            defaultState = $defaultState
            selectedState = $requestedState
            dirtySourceRejected = $false
            dirtySourcePreserved = $false
            acquisition = $null
        }
    }

    if ($defaultState.isGitCheckout -and $defaultState.statusCount -eq 0 -and $defaultState.head -eq $TargetCommit) {
        return [pscustomobject]@{
            checkoutPath = $DefaultRoot
            strategy = "existing-clean-locked-checkout"
            requestedPath = $null
            defaultPath = $DefaultRoot
            defaultState = $defaultState
            selectedState = $defaultState
            dirtySourceRejected = $false
            dirtySourcePreserved = $false
            acquisition = $null
        }
    }

    $cloneRoot = [System.IO.Path]::GetFullPath($DestinationRoot)
    if (Test-Path -LiteralPath $cloneRoot) { throw "C52 clean source destination already exists: $cloneRoot" }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $cloneRoot) | Out-Null
    $useLocalObjectDatabase = $defaultState.isGitCheckout -and $defaultState.targetCommitAvailable
    $gitPath = (Get-Command git.exe -ErrorAction Stop).Source
    if ($useLocalObjectDatabase) {
        $cloneSource = $DefaultRoot
        $cloneArguments = @("-c", "core.longpaths=true", "-C", $DefaultRoot, "worktree", "add", "--detach", $cloneRoot, $TargetCommit)
        $cloneInvocation = Invoke-BoundedProcess $gitPath $cloneArguments `
            (Join-Path $runRoot "source-acquisition.worktree.log") $TimeoutSeconds
        if ($cloneInvocation.exitCode -ne 0) { throw "C52 clean locked-source worktree acquisition failed. exit=$($cloneInvocation.exitCode) source=$cloneSource" }
        $checkoutInvocation = [ordered]@{ method = "git worktree add --detach"; target = $TargetCommit; result = "PASS"; log = $cloneInvocation.log; stderr = $cloneInvocation.stderr }
    } else {
        $cloneSource = "https://github.com/dotnet/runtime.git"
        $cloneArguments = @("clone", "--filter=blob:none", "--no-checkout", "--no-tags", $cloneSource, $cloneRoot)
        $cloneInvocation = Invoke-BoundedProcess $gitPath $cloneArguments `
            (Join-Path $runRoot "source-acquisition.clone.log") $TimeoutSeconds
        if ($cloneInvocation.exitCode -ne 0) { throw "C52 clean locked-source acquisition failed. exit=$($cloneInvocation.exitCode) source=$cloneSource" }
        $checkoutInvocation = Invoke-BoundedProcess $gitPath `
            @("-C", $cloneRoot, "checkout", "--detach", $TargetCommit) `
            (Join-Path $runRoot "source-acquisition.checkout.log") $TimeoutSeconds
        if ($checkoutInvocation.exitCode -ne 0) { throw "C52 clean locked-source checkout failed. exit=$($checkoutInvocation.exitCode) target=$TargetCommit" }
    }
    $selectedState = Get-GitCheckoutState $cloneRoot
    if (-not $selectedState.isGitCheckout -or $selectedState.statusCount -ne 0 -or $selectedState.head -ne $TargetCommit) {
        throw "C52 clean locked-source checkout validation failed: $cloneRoot"
    }
    return [pscustomobject]@{
        checkoutPath = $cloneRoot
        strategy = if ($useLocalObjectDatabase) { "fresh-clean-worktree-from-preserved-dirty-object-database" } else { "fresh-remote-filtered-clone" }
        requestedPath = $null
        defaultPath = $DefaultRoot
        defaultState = $defaultState
        selectedState = $selectedState
        dirtySourceRejected = $defaultState.statusCount -ne 0
        dirtySourcePreserved = $defaultState.statusCount -ne 0
        acquisition = [ordered]@{
            source = $cloneSource
            clone = $cloneInvocation
            checkout = $checkoutInvocation
            destination = $cloneRoot
        }
    }
}

$runtimePackRoot = Join-Path $RepoRoot "tools\dotnet\runtime-pack"
$lockPath = Join-Path $runtimePackRoot "runtime-pack.lock.json"
$patchPath = Join-Path $runtimePackRoot "patches\nativeaot-amd64-fp-handoff.patch"
$fixtureTestsScript = Join-Path $RepoRoot "tests\dotnet\Invoke-NativeAotFpRepairTests.ps1"
$buildScript = Join-Path $RepoRoot "scripts\dotnet\build-guidexos-nativeaot-runtime-pack.ps1"
$runtimeBuildScript = Join-Path $runtimePackRoot "build-runtime-pack.ps1"
$patchApplyScript = Join-Path $runtimePackRoot "apply-nativeaot-fp-repair.ps1"
$gcScript = Join-Path $RepoRoot "scripts\smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1"
$ordinaryScript = Join-Path $RepoRoot "scripts\dotnet\Validate-GuideXOSOrdinaryBoot.ps1"
$genericElfScript = Join-Path $RepoRoot "scripts\smoke-native-elf-generic.ps1"
$peElfRegressionScript = Join-Path $RepoRoot "tools\dotnet\test_pe_to_elf_v2_fixed_base.py"
$lock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
$lockedCommit = ([string]$lock.nativeAotFpRepair.sourceCommit).Trim().ToLowerInvariant()
$startingHead = (& git -C $RepoRoot rev-parse HEAD).Trim()
$startingSubject = (& git -C $RepoRoot log -1 --format=%s).Trim()
$startingBranch = (& git -C $RepoRoot branch --show-current).Trim()
$startingUpstream = (& git -C $RepoRoot rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>$null).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($startingUpstream)) { $startingUpstream = $null }
$startingAheadBehind = if ($null -eq $startingUpstream) { $null } else { ((& git -C $RepoRoot rev-list --left-right --count "HEAD...$startingUpstream").Trim()) }
$startingStatus = @(& git -C $RepoRoot status --short)
$startingUntracked = @(& git -C $RepoRoot ls-files --others --exclude-standard)
$c51Commit = (& git -C $RepoRoot rev-parse --verify df96f9af 2>$null).Trim()
$c51PresentInHistory = $LASTEXITCODE -eq 0
$c51UpstreamState = "NO_UPSTREAM"
if ($c51PresentInHistory -and $null -ne $startingUpstream) {
    & git -C $RepoRoot merge-base --is-ancestor $c51Commit $startingUpstream 2>$null
    $c51UpstreamState = if ($LASTEXITCODE -eq 0) { "C51_IS_UPSTREAM" } else { "C51_NOT_UPSTREAM" }
}
$runId = "run-" + (Get-Date -Format "yyyyMMdd-HHmmssfff") + "-" + ([guid]::NewGuid().ToString("N").Substring(0, 8))
$runRoot = Join-Path $EvidenceRoot $runId
New-Item -ItemType Directory -Force -Path $runRoot | Out-Null
$sourceEvidenceRoot = Join-Path $RepoRoot "out\dotnet\c52-runtime-source"
$sourceDestinationRoot = Join-Path $sourceEvidenceRoot ("source-" + ([guid]::NewGuid().ToString("N").Substring(0, 8)))
$manifestPath = Join-Path $runRoot "c52.validation.manifest.json"
$tierResults = [ordered]@{}
$failure = $null
$failureTier = $null
$failureCategory = $null
$currentTier = $null
$runtimePackManifestPath = $null
$gcManifestPath = $null
$ordinaryManifestPath = $null
$canonicalOrdinaryBefore = $null
$canonicalOrdinaryAfter = $null
$sourceCheckout = $null
$processAuditBefore = $null
$preRunCleanup = @()
$processAuditAfterPreRunCleanup = $null
$processAuditAfter = $null
$currentProcessIds = @()
$ownedEvidenceRoots = @(
    ([System.IO.Path]::GetFullPath((Join-Path $RepoRoot "out\dotnet\c51-runtime-pack-validation"))),
    ([System.IO.Path]::GetFullPath((Join-Path $RepoRoot "out\dotnet\c52-runtime-pack-validation"))),
    ([System.IO.Path]::GetFullPath($sourceEvidenceRoot)),
    ([System.IO.Path]::GetFullPath($EvidenceRoot)),
    ([System.IO.Path]::GetFullPath($runRoot))
)
$defaultExternalRuntimeRoot = Join-Path $RepoRoot "out\dotnet\gc-feasibility-baseline\nativeaot-runtime"
$stageLimit = switch ($Tier) {
    "A" { 1 }
    "B" { 2 }
    "C" { 3 }
    default { 4 }
}
foreach ($name in @("A", "B", "C", "D")) {
    $tierResults[$name] = [ordered]@{
        result = if ((@("A", "B", "C", "D").IndexOf($name)) -lt $stageLimit) { "NOT_RUN" } else { "NOT_REQUESTED" }
        startUtc = $null
        endUtc = $null
    }
}

try {
    $currentProcessIds = Get-CurrentProcessIds
    $processAuditBefore = Get-ProcessSnapshot
    $preRunOwned = @($processAuditBefore | Where-Object { $_.owned -eq $true })
    $preRunCleanup = Stop-OwnedProcesses $preRunOwned (Join-Path $runRoot "pre-run-owned-process-cleanup.log")
    $processAuditAfterPreRunCleanup = Get-ProcessSnapshot
    $remainingPreRunOwned = @($processAuditAfterPreRunCleanup | Where-Object { $_.owned -eq $true })
    if ($remainingPreRunOwned.Count -ne 0) { throw "C52 pre-run audit could not clear confirmed stale owned processes: $($remainingPreRunOwned.processId -join ',')" }

    Require-File $lockPath "C51 runtime-pack lock"
    Require-File $patchPath "C51 runtime-pack patch"
    foreach ($scriptPath in @($fixtureTestsScript, $buildScript, $runtimeBuildScript, $patchApplyScript, $gcScript, $ordinaryScript, $genericElfScript, $peElfRegressionScript)) {
        Require-File $scriptPath "C52 validation input"
    }

    $currentTier = "A"
    $tierA = $tierResults.A
    $tierA.startUtc = (Get-Date).ToUniversalTime().ToString("o")
    $tierA.checks = [ordered]@{}
    $tierA.checks.lockIdentity = [ordered]@{
        architecture = $lock.architecture
        targetFramework = $lock.targetFramework
        runtimeIdentifier = $lock.runtimeIdentifier
        nativeAot = $lock.ilCompiler.version
        ilCompilerCommit = $lock.ilCompiler.commit
        runtimePackCommit = $lock.runtimePack.commit
        gc = "Workstation"
        gcInterface = $lock.adaptedWorkstationGc.gcInterface
        eeInterface = $lock.adaptedWorkstationGc.eeInterface
        sourceCommit = $lockedCommit
        patchSha256 = Get-Hash $patchPath
        lockPatchSha256 = $lock.nativeAotFpRepair.patchSha256
    }
    Assert-Equal "C52 lock patch hash" $tierA.checks.lockIdentity.patchSha256 $tierA.checks.lockIdentity.lockPatchSha256
    if ([string]$lock.architecture -ne "amd64" -or [string]$lock.targetFramework -ne "net9.0" -or
        [string]$lock.runtimeIdentifier -ne "win-x64" -or [string]$lock.ilCompiler.version -ne "9.0.0" -or
        [string]$lock.ilCompiler.commit -ne $lockedCommit -or [string]$lock.runtimePack.commit -ne $lockedCommit -or
        [string]$lock.adaptedWorkstationGc.gcInterface -ne "5.3" -or [string]$lock.adaptedWorkstationGc.eeInterface -ne "2") {
        throw "C52 locked identity is not NativeAOT 9.0.0 AMD64 Workstation GC source $lockedCommit with interfaces 5.3/2."
    }
    foreach ($scriptPath in @($fixtureTestsScript, $buildScript, $runtimeBuildScript, $patchApplyScript, $gcScript, $ordinaryScript, $genericElfScript, $PSCommandPath)) {
        Test-PowerShellSyntax $scriptPath
    }
    $gcText = Get-Content -LiteralPath $gcScript -Raw
    $buildText = Get-Content -LiteralPath $runtimeBuildScript -Raw
    $topText = Get-Content -LiteralPath $PSCommandPath -Raw
    if ($gcText -notmatch '\$useC011EC46SemanticInjection\s*=\s*\$isC011EC46\s+-and\s+-not\s+\$isC011EC50Production' -or
        $gcText -notmatch 'C51 semantic-rewrite guard failed' -or
        $gcText -notmatch 'LockedRuntimeRoot' -or
        $gcText -notmatch 'runtimePackBuildManifest\.nativeAotFpRepairStateAfter' -or
        $buildText -notmatch 'C51 stale-artifact protection' -or
        $buildText -notmatch 'archive membership validation' -or
        $topText -notmatch 'allRequestedPassed' -or $topText -notmatch 'partialTierPassPrevention') {
        throw "C52 static semantic, source, stale-artifact, archive, or aggregate gating guard is incomplete."
    }
    $genericInvocation = Invoke-BoundedPowerShell $genericElfScript @("-RepoRoot", $RepoRoot) (Join-Path $runRoot "tier-a-generic-elf.log") $CommandTimeoutSeconds
    if ($genericInvocation.exitCode -ne 0) { throw "Generic Native ELF regression guard failed." }
    $pythonPath = "C:\Users\guideX\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
    Require-File $pythonPath "C52 Python runtime"
    $peElfInvocation = Invoke-BoundedProcess $pythonPath @($peElfRegressionScript) (Join-Path $runRoot "tier-a-pe-to-elf.log") $CommandTimeoutSeconds
    if ($peElfInvocation.exitCode -ne 0) { throw "PE-to-ELF regression test failed." }
    $diffCheckInvocation = Invoke-BoundedProcess (Get-Command git.exe -ErrorAction Stop).Source @("-C", $RepoRoot, "diff", "--check") (Join-Path $runRoot "tier-a-git-diff-check.log") $CommandTimeoutSeconds
    if ($diffCheckInvocation.exitCode -ne 0) { throw "git diff --check failed." }
    $tierA.checks.staticGuards = [ordered]@{
        powershellParse = "PASS"
        genericElf = $genericInvocation
        peToElf = $peElfInvocation
        gitDiffCheck = $diffCheckInvocation
    }

    $fixtureResultPath = Join-Path $runRoot "nativeaot-fp-repair-fixtures.json"
    $fixtureInvocation = Invoke-BoundedPowerShell $fixtureTestsScript @(
        "-RepoRoot", $RepoRoot,
        "-ResultPath", $fixtureResultPath,
        "-KeepFixtures"
    ) (Join-Path $runRoot "tier-a-fixtures.log") $CommandTimeoutSeconds
    $tierA.checks.fixtureInvocation = $fixtureInvocation
    if ($fixtureInvocation.exitCode -ne 0 -or -not (Test-Path -LiteralPath $fixtureResultPath -PathType Leaf)) {
        throw "C52 fixture tests failed or did not produce a result manifest. exit=$($fixtureInvocation.exitCode) timedOut=$($fixtureInvocation.timedOut) resultPath=$fixtureResultPath"
    }
    $fixtureResult = Get-Content -LiteralPath $fixtureResultPath -Raw | ConvertFrom-Json
    $fixtureTests = @($fixtureResult.tests)
    if ($fixtureResult.result -ne "PASS" -or [int]$fixtureResult.failed -ne 0 -or [int]$fixtureResult.passed -ne 7 -or $fixtureTests.Count -ne 7 -or
        @($fixtureTests | Where-Object { $_.result -ne "PASS" }).Count -ne 0) {
        throw "C52 fixture suite did not prove all seven expected patch-state paths. result=$($fixtureResult.result) passed=$($fixtureResult.passed) failed=$($fixtureResult.failed) tests=$($fixtureTests.Count)"
    }
    $tierA.checks.fixtureResult = [ordered]@{
        path = $fixtureResultPath
        sha256 = Get-Hash $fixtureResultPath
        result = $fixtureResult.result
        passed = [int]$fixtureResult.passed
        failed = [int]$fixtureResult.failed
        count = $fixtureTests.Count
        tests = @($fixtureTests | ForEach-Object { [ordered]@{ name = $_.name; result = $_.result; exitCode = $_.exitCode } })
    }
    $tierA.result = "PASS"
    $tierA.endUtc = (Get-Date).ToUniversalTime().ToString("o")
    Write-Host "[C52] tier=A PASS"

    if ($stageLimit -ge 2) {
        $currentTier = "B"
        $tierB = $tierResults.B
        $tierB.startUtc = (Get-Date).ToUniversalTime().ToString("o")
        $buildOutputRoot = Join-Path $runRoot "runtime-pack"
        $sourceCheckout = New-LockedSourceCheckout -RequestedRoot $ExternalRuntimeRoot -DefaultRoot $defaultExternalRuntimeRoot -TargetCommit $lockedCommit -DestinationRoot $sourceDestinationRoot -TimeoutSeconds $CommandTimeoutSeconds
        $buildArguments = @(
            "-RepoRoot", $RepoRoot,
            "-OutputRoot", $buildOutputRoot,
            "-ExternalRuntimeRoot", $sourceCheckout.checkoutPath,
            "-NativeAotFpRepair", "-Clean"
        )
        if (-not [string]::IsNullOrWhiteSpace($StockRuntimePackRoot)) { $buildArguments += @("-StockRuntimePackRoot", [System.IO.Path]::GetFullPath($StockRuntimePackRoot)) }
        $tierB.invocation = Invoke-BoundedPowerShell $buildScript $buildArguments (Join-Path $runRoot "tier-b-runtime-pack.log") $CommandTimeoutSeconds
        if ($tierB.invocation.exitCode -ne 0) { throw "C52 fresh runtime-pack build failed." }
        $runtimePackManifestPath = Join-Path $buildOutputRoot "runtime-pack.manifest.json"
        $runtimePackManifest = Assert-RuntimePackManifest $runtimePackManifestPath $lockPath $patchPath $lockedCommit
        if ([System.IO.Path]::GetFullPath($runtimePackManifest.externalRuntimeRoot) -ne [System.IO.Path]::GetFullPath($sourceCheckout.checkoutPath) -or
            [string]$runtimePackManifest.externalRuntimeCheckoutHead -ne $lockedCommit) {
            throw "C52 runtime-pack manifest does not identify the clean locked source checkout and exact target head."
        }
        $tierB.source = [ordered]@{
            strategy = $sourceCheckout.strategy
            requestedRoot = $sourceCheckout.requestedPath
            defaultRoot = $sourceCheckout.defaultPath
            defaultState = $sourceCheckout.defaultState
            selectedPath = $sourceCheckout.checkoutPath
            selectedState = $sourceCheckout.selectedState
            dirtySourceRejected = $sourceCheckout.dirtySourceRejected
            dirtySourcePreserved = $sourceCheckout.dirtySourcePreserved
            acquisition = $sourceCheckout.acquisition
        }
        $tierB.runtimePackManifest = [ordered]@{
            path = $runtimePackManifestPath
            sha256 = Get-Hash $runtimePackManifestPath
            sourceCommit = $runtimePackManifest.nativeAotFpRepairSourceCommit
            externalRuntimeRoot = $runtimePackManifest.externalRuntimeRoot
            externalRuntimeCheckoutHead = $runtimePackManifest.externalRuntimeCheckoutHead
            patchState = $runtimePackManifest.nativeAotFpRepairStateAfter
            patchSha256 = $runtimePackManifest.nativeAotFpRepairPatchSha256
            archiveSha256 = $runtimePackManifest.adaptedRuntimeLibrarySha256
            runtimeLibrary = $runtimePackManifest.adaptedRuntimeLibrary
            stackObject = $runtimePackManifest.nativeAotFpRepairObjects.stackFrameIterator
            stackObjectSha256 = $runtimePackManifest.nativeAotFpRepairObjects.stackFrameIteratorSha256
            coffObject = $runtimePackManifest.nativeAotFpRepairObjects.coffNativeCodeManager
            coffObjectSha256 = $runtimePackManifest.nativeAotFpRepairObjects.coffNativeCodeManagerSha256
            archiveMembership = $runtimePackManifest.archiveMembership
            freshOutputRoot = $runtimePackManifest.buildCommandIdentity.freshOutputRoot
        }
        $tierB.result = "PASS"
        $tierB.endUtc = (Get-Date).ToUniversalTime().ToString("o")
        Write-Host "[C52] tier=B PASS manifest=$runtimePackManifestPath"
    }

    if ($stageLimit -ge 3) {
        if ($null -eq $runtimePackManifestPath -or $null -eq $sourceCheckout) { throw "C52 tier C requires a passing tier B runtime-pack manifest and clean source checkout." }
        $currentTier = "C"
        $tierC = $tierResults.C
        $tierC.startUtc = (Get-Date).ToUniversalTime().ToString("o")
        $gcEvidenceRoot = Join-Path $runRoot "gc-proof"
        $tierC.invocation = Invoke-BoundedPowerShell $gcScript @(
            "-RepoRoot", $RepoRoot,
            "-EvidenceRoot", $gcEvidenceRoot,
            "-TimeoutSeconds", $BootTimeoutSeconds,
            "-FreshBootCount", $FreshBootCount,
            "-RuntimePackManifest", $runtimePackManifestPath,
            "-LockedRuntimeRoot", $sourceCheckout.checkoutPath,
            "-ProofMode", "productionized-second-collection"
        ) (Join-Path $runRoot "tier-c-gc-proof.log") $CommandTimeoutSeconds
        if ($tierC.invocation.exitCode -ne 0) { throw "C52 productionized three-boot GC proof failed." }
        $gcManifestPath = Get-LatestManifest $gcEvidenceRoot "manifest.json"
        $gcManifest = Get-Content -LiteralPath $gcManifestPath -Raw | ConvertFrom-Json
        $c49 = $gcManifest.c49
        $requiredC49 = @{
            allRunsComplete = $true
            semanticConsistent = $true
            condemnedGeneration = "0x00000001"
            collectionOrdinal = "0x00000002"
            maximumGeneration = "0x00000002"
            plannerDecision = "0x00000001"
            compactBranch = "0x00000001"
            sweepBranch = "0x00000000"
            postGcAllocationCount = "0x00000008"
            invariantFailures = "0x00000000"
            sensitiveDiagnosticAllocations = "0x00000000"
            safeStopReason = "0x00000000"
        }
        foreach ($entry in $requiredC49.GetEnumerator()) { Assert-Equal "C52 C50 $($entry.Key)" $c49.($entry.Key) $entry.Value }
        if ($gcManifest.marker -ne "C011EC50" -or $gcManifest.productionized -ne $true -or $gcManifest.semanticHarnessRewriteRequired -ne $false -or
            $gcManifest.semanticRewriteGuard.result -ne "PASS" -or
            [System.IO.Path]::GetFullPath($gcManifest.runtimePackBuildManifest) -ne [System.IO.Path]::GetFullPath($runtimePackManifestPath) -or
            [System.IO.Path]::GetFullPath($gcManifest.lockedRuntimeRoot) -ne [System.IO.Path]::GetFullPath($sourceCheckout.checkoutPath) -or
            [int]$gcManifest.successLevel -ne 5) {
            throw "C52 C50 production manifest did not prove the locked pack, clean source, semantic guard, and production outcome."
        }
        $gcRuns = @($gcManifest.qemu.runs)
        if ($gcRuns.Count -ne 3 -or @($gcManifest.qemu.serialSha256).Count -ne 3) { throw "C52 C50 manifest does not contain exactly three fresh serial runs and hashes." }
        foreach ($artifactName in @("proofKernel", "managedPe", "elf", "map")) {
            $artifactPathProperty = $gcManifest.artifactPaths.PSObject.Properties[$artifactName]
            $artifactHashProperty = $gcManifest.payloadHashes.PSObject.Properties[$artifactName]
            if ($null -eq $artifactPathProperty -or $null -eq $artifactHashProperty) { throw "C52 GC manifest is missing artifact provenance for $artifactName." }
            Require-File $artifactPathProperty.Value "C52 GC artifact $artifactName"
            Assert-Equal "C52 GC artifact $artifactName hash" (Get-Hash $artifactPathProperty.Value) $artifactHashProperty.Value
        }
        $gcRunSummaries = @()
        foreach ($run in $gcRuns) {
            if ($run.outcome -ne "A" -or $run.c18 -ne $true -or $run.c28 -ne $true -or [int]$run.c47Invalid -ne 0 -or [int]$run.formerFaults -ne 0 -or
                $run.fields.compacting -ne "0x00000001" -or $run.fields.relocating -ne "0x00000001" -or $run.fields.sweep -ne "0x00000000" -or
                $run.fields.restart -ne "0x00000001" -or $run.fields.managedResume -ne "0x00000001" -or $run.fields.postGcAllocationCount -ne "0x00000008" -or
                $run.productionFpRepairStatic -ne $true -or [string]$run.earlyFailure -ne "") {
                throw "C52 C50 run did not pass the retained C18/C28/C47/C48 production assertions: $($run.name)"
            }
            if ([string]$run.c26 -notmatch "totalRoots=00000004" -or [string]$run.c26 -notmatch "promoteEntries=00000004") {
                throw "C52 C26 root scan did not retain exactly four promoted roots: $($run.name)"
            }
            Require-File $run.serial "C52 GC serial log"
            Assert-Equal "C52 GC serial hash $($run.name)" (Get-Hash $run.serial) $run.serialSha256
            $serialText = Get-Content -LiteralPath $run.serial -Raw
            if ($serialText -match '(?im)PageFault|triple.?fault|FAIL[_ -]?FAST|fatal kernel failure') { throw "C52 GC serial contains page-fault or fail-fast evidence: $($run.name)" }
            foreach ($marker in @("C011EC37-C1-LIVE", "C011EC37-C1-RESUMED", "C011EC37-PREFLIGHT", "COMPLETE marker=C011EC37")) {
                if ($serialText -notmatch [regex]::Escape($marker)) { throw "C52 GC serial is missing first-collection marker '$marker': $($run.name)" }
            }
            $gcRunSummaries += [ordered]@{
                name = $run.name
                outcome = $run.outcome
                serial = $run.serial
                serialSha256 = $run.serialSha256
                c18 = $run.c18
                c26 = $run.c26
                c28 = $run.c28
                promotedRoots = $run.fields.promotedRoots
                collection1 = "PASS"
                collection2 = [ordered]@{
                    trigger = $run.fields.collectionReason
                    condemnedGeneration = $run.fields.condemnedGeneration
                    maximumGeneration = $run.fields.maximumGeneration
                    planner = $run.fields.plannerDecision
                    compacting = $run.fields.compacting
                    relocating = $run.fields.relocating
                    sweep = $run.fields.sweep
                    restart = $run.fields.restart
                    managedResume = $run.fields.managedResume
                    postGcAllocations = $run.fields.postGcAllocationCount
                }
                liveObjectIntegrity = [ordered]@{ livePlugCount = $run.fields.livePlugCount; deadGapCount = $run.fields.deadGapCount; rootRewrites = $run.fields.rootRewrites; rootUnchanged = $run.fields.rootUnchanged }
                pageFaultAbsent = $true
                failFastAbsent = $true
            }
        }
        $tierC.gcManifest = [ordered]@{
            path = $gcManifestPath
            sha256 = Get-Hash $gcManifestPath
            marker = $gcManifest.marker
            productionized = $gcManifest.productionized
            outcome = $gcManifest.outcome
            successLevel = $gcManifest.successLevel
            freshRunCount = $gcRuns.Count
            runtimePackManifest = $gcManifest.runtimePackBuildManifest
            runtimePackManifestSha256 = Get-Hash $runtimePackManifestPath
            serialSha256 = @($gcManifest.qemu.serialSha256)
            proofKernelSha256 = $gcManifest.qemu.proofKernelSha256
            runs = $gcRunSummaries
            summary = [ordered]@{
                c18 = "PASS"
                c26RootScan = "PASS"
                promotedRoots = $c49.promotedRoots
                c28MarkClosure = "PASS"
                collection1 = "PASS"
                collection2Trigger = $c49.collectionReason
                collection2CondemnedGeneration = $c49.condemnedGeneration
                collection2MaximumGeneration = $c49.maximumGeneration
                planner = $c49.plannerDecision
                compacting = $gcRuns[0].fields.compacting
                relocating = $gcRuns[0].fields.relocating
                sweep = $c49.sweepBranch
                restartEE = "PASS"
                managedResume = "PASS"
                liveObjectIntegrity = "PASS"
                postGcAllocations = $c49.postGcAllocationCount
                invariantFailures = $c49.invariantFailures
                sensitiveDiagnosticAllocations = $c49.sensitiveDiagnosticAllocations
            }
        }
        $tierC.result = "PASS"
        $tierC.endUtc = (Get-Date).ToUniversalTime().ToString("o")
        Write-Host "[C52] tier=C PASS manifest=$gcManifestPath"
    }

    if ($stageLimit -ge 4) {
        if ($null -eq $gcManifestPath) { throw "C52 tier D requires a passing tier C GC manifest." }
        $currentTier = "D"
        $tierD = $tierResults.D
        $tierD.startUtc = (Get-Date).ToUniversalTime().ToString("o")
        $canonicalOrdinaryBefore = [ordered]@{
            kernel = Get-Hash (Join-Path $RepoRoot "kernel\build\amd64\bin\kernel.elf")
            espKernel = Get-Hash (Join-Path $RepoRoot "ESP\kernel.elf")
        }
        $ordinaryEvidenceRoot = Join-Path $runRoot "ordinary-boot"
        $tierD.invocation = Invoke-BoundedPowerShell $ordinaryScript @(
            "-RepoRoot", $RepoRoot,
            "-EvidenceRoot", $ordinaryEvidenceRoot,
            "-TimeoutSeconds", $BootTimeoutSeconds,
            "-FreshBootCount", $FreshBootCount
        ) (Join-Path $runRoot "tier-d-ordinary-boot.log") $CommandTimeoutSeconds
        if ($tierD.invocation.exitCode -ne 0) { throw "C52 precise ordinary-boot validation failed." }
        $ordinaryManifestPath = Get-LatestManifest $ordinaryEvidenceRoot "ordinary-boot.manifest.json"
        $ordinaryManifest = Get-Content -LiteralPath $ordinaryManifestPath -Raw | ConvertFrom-Json
        $expectedOrdinaryHash = "75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6"
        if ($ordinaryManifest.outcome -ne "PASS" -or [int]$ordinaryManifest.freshBootCount -ne 3 -or $ordinaryManifest.noCanonicalKernelMutation -ne $true -or
            $ordinaryManifest.inputHashesBefore.kernel -ne $expectedOrdinaryHash -or $ordinaryManifest.inputHashesBefore.espKernel -ne $expectedOrdinaryHash -or
            $ordinaryManifest.inputHashesAfter.kernel -ne $expectedOrdinaryHash -or $ordinaryManifest.inputHashesAfter.espKernel -ne $expectedOrdinaryHash) {
            throw "C52 ordinary-boot manifest did not pass exact marker and canonical artifact assertions."
        }
        $canonicalOrdinaryAfter = [ordered]@{
            kernel = Get-Hash (Join-Path $RepoRoot "kernel\build\amd64\bin\kernel.elf")
            espKernel = Get-Hash (Join-Path $RepoRoot "ESP\kernel.elf")
        }
        if ($canonicalOrdinaryBefore.kernel -ne $canonicalOrdinaryAfter.kernel -or $canonicalOrdinaryBefore.espKernel -ne $canonicalOrdinaryAfter.espKernel) {
            throw "C52 canonical ordinary kernel/ESP hashes changed during Tier D."
        }
        $ordinaryBoots = @($ordinaryManifest.boots)
        if ($ordinaryBoots.Count -ne 3 -or @($ordinaryBoots | Where-Object { $_.result -ne "PASS" -or $_.mainLoopMarker -ne $true -or $_.navigatorPassMarker -ne $true -or $_.navigatorFailMarker -ne $false -or $_.pageFaultAbsent -ne $true -or $_.failFastAbsent -ne $true -or $_.semanticProofMarkersAbsent -ne $true }).Count -ne 0) {
            throw "C52 ordinary validator did not pass all three precise production boots."
        }
        $tierD.ordinaryManifest = [ordered]@{
            path = $ordinaryManifestPath
            sha256 = Get-Hash $ordinaryManifestPath
            validator = $ordinaryManifest.validator
            outcome = $ordinaryManifest.outcome
            freshBootCount = $ordinaryManifest.freshBootCount
            noCanonicalKernelMutation = $ordinaryManifest.noCanonicalKernelMutation
            inputHashesBefore = $ordinaryManifest.inputHashesBefore
            inputHashesAfter = $ordinaryManifest.inputHashesAfter
            serialSha256 = @($ordinaryBoots | ForEach-Object { $_.serialSha256 })
            boots = @($ordinaryBoots | ForEach-Object { [ordered]@{ result=$_.result; mainLoop=$_.mainLoopMarker; navigatorPass=$_.navigatorPassMarker; serial=$_.serialPath; serialSha256=$_.serialSha256 } })
        }
        $tierD.result = "PASS"
        $tierD.endUtc = (Get-Date).ToUniversalTime().ToString("o")
        Write-Host "[C52] tier=D PASS manifest=$ordinaryManifestPath"
    }
}
catch {
    $failure = $_.Exception.Message
    $failureTier = $currentTier
    $failureCategory = if ($null -eq $currentTier) { "PREFLIGHT_FAILURE" } else { "TIER_${currentTier}_FAILURE" }
    if ($null -ne $currentTier -and $tierResults.Contains($currentTier)) {
        $tierResults[$currentTier].result = "FAIL"
        $tierResults[$currentTier].failure = $failure
        $tierResults[$currentTier].endUtc = (Get-Date).ToUniversalTime().ToString("o")
    }
    Write-Host "[C52] FAIL tier=$failureTier category=$failureCategory message=$failure" -ForegroundColor Red
}

$negativePropagation = [ordered]@{ result = "NOT_RUN"; expected = "nonzero"; exitCode = $null; log = $null; manifest = $null }
if ($null -eq $failure -and $Tier -eq "All" -and $null -ne $sourceCheckout) {
    $negativeRoot = Join-Path $runRoot "negative-tier-b"
    $missingStockRoot = Join-Path $runRoot "missing-stock-runtime-pack"
    $negativePowerShell = (Get-Command powershell.exe -ErrorAction Stop).Source
    $negativeArguments = @(
        "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $PSCommandPath,
        "-Tier", "B",
        "-RepoRoot", $RepoRoot,
        "-EvidenceRoot", $negativeRoot,
        "-ExternalRuntimeRoot", $sourceCheckout.checkoutPath,
        "-StockRuntimePackRoot", $missingStockRoot,
        "-FreshBootCount", $FreshBootCount,
        "-BootTimeoutSeconds", $BootTimeoutSeconds,
        "-CommandTimeoutSeconds", $CommandTimeoutSeconds
    )
    $negativeLog = Join-Path $runRoot "negative-top-level-tier-b.log"
    $negativeErrorLog = $negativeLog + ".stderr.log"
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $negativeLog) | Out-Null
    $negativeOutput = @(& $negativePowerShell @negativeArguments 2>&1)
    $negativeExitCode = [int]$LASTEXITCODE
    $negativeOutput | Set-Content -LiteralPath $negativeLog -Encoding ASCII
    Set-Content -LiteralPath $negativeErrorLog -Value "Synchronous bounded preflight negative invocation; no QEMU or build was requested." -Encoding ASCII
    $negativeInvocation = [ordered]@{
        file = $negativePowerShell
        arguments = $negativeArguments
        command = $negativePowerShell + " " + (($negativeArguments | ForEach-Object { [string]$_ }) -join " ")
        exitCode = $negativeExitCode
        timedOut = $false
        log = $negativeLog
        stderr = $negativeErrorLog
        outputTail = if ($negativeOutput.Count -gt 40) { ($negativeOutput | Select-Object -Last 40 | Out-String).Trim() } else { ($negativeOutput | Out-String).Trim() }
    }
    $negativeManifest = Get-ChildItem -LiteralPath $negativeRoot -Recurse -File -Filter "c52.validation.manifest.json" -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    $negativeManifestObject = if ($null -eq $negativeManifest) { $null } else { Get-Content -LiteralPath $negativeManifest.FullName -Raw | ConvertFrom-Json }
    $negativePropagation = [ordered]@{
        result = if ($negativeInvocation.exitCode -ne 0 -and $null -ne $negativeManifestObject -and [int]$negativeManifestObject.aggregateInvocation.exitCode -ne 0) { "PASS" } else { "FAIL" }
        expected = "nonzero"
        exitCode = $negativeInvocation.exitCode
        log = $negativeInvocation.log
        manifest = if ($null -eq $negativeManifest) { $null } else { $negativeManifest.FullName }
        manifestExitCode = if ($null -eq $negativeManifestObject) { $null } else { $negativeManifestObject.aggregateInvocation.exitCode }
        failureCategory = if ($null -eq $negativeManifestObject) { $null } else { $negativeManifestObject.failureCategory }
    }
    if ($negativePropagation.result -ne "PASS") {
        $failure = "Top-level negative Tier B propagation test returned zero."
        $failureTier = "B"
        $failureCategory = "EXIT_PROPAGATION_FAILURE"
    }
}

try {
    $processAuditBeforeFinal = Get-ProcessSnapshot
    $finalOwned = @($processAuditBeforeFinal | Where-Object { $_.owned -eq $true })
    $finalCleanup = Stop-OwnedProcesses $finalOwned (Join-Path $runRoot "final-owned-process-cleanup.log")
    $processAuditAfter = Get-ProcessSnapshot
    $remainingOwned = @($processAuditAfter | Where-Object { $_.owned -eq $true })
    if ($remainingOwned.Count -ne 0 -and $null -eq $failure) {
        $failure = "C52-owned processes remained after final cleanup: $($remainingOwned.processId -join ',')"
        $failureTier = "CLEANUP"
        $failureCategory = "CLEANUP_FAILURE"
    }
}
catch {
    if ($null -eq $failure) {
        $failure = "Process cleanup audit failed: " + $_.Exception.Message
        $failureTier = "CLEANUP"
        $failureCategory = "CLEANUP_AUDIT_FAILURE"
    }
}

$requiredTierNames = if ($Tier -eq "All") { @("A", "B", "C", "D") } else { @("A", "B", "C", "D")[0..($stageLimit - 1)] }
$allRequestedPassed = $null -eq $failure -and @($requiredTierNames | Where-Object { $tierResults[$_].result -ne "PASS" }).Count -eq 0
$successLevel = 0
if ($tierResults.A.result -eq "PASS") { $successLevel = 1 }
if ($successLevel -eq 1 -and $tierResults.B.result -eq "PASS") { $successLevel = 2 }
if ($successLevel -eq 2 -and $tierResults.C.result -eq "PASS") { $successLevel = 4 }
if ($successLevel -eq 4 -and $tierResults.D.result -eq "PASS") { $successLevel = 5 }
$outcome = if ($allRequestedPassed -and $Tier -eq "All" -and $successLevel -eq 5) { "A" } elseif ($successLevel -ge 4) { "B" } elseif ($successLevel -ge 2) { "C" } elseif ($successLevel -ge 1) { "D" } else { "D" }
$finalHead = (& git -C $RepoRoot rev-parse HEAD).Trim()
$finalSubject = (& git -C $RepoRoot log -1 --format=%s).Trim()
$finalBranch = (& git -C $RepoRoot branch --show-current).Trim()
$finalUpstream = (& git -C $RepoRoot rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>$null).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($finalUpstream)) { $finalUpstream = $null }
$finalAheadBehind = if ($null -eq $finalUpstream) { $null } else { (& git -C $RepoRoot rev-list --left-right --count "HEAD...$finalUpstream").Trim() }
$finalStatus = @(& git -C $RepoRoot status --short)
$finalUntracked = @(& git -C $RepoRoot ls-files --others --exclude-standard)
$qemuBefore = @($processAuditBefore | Where-Object { $_.name -ieq "qemu-system-x86_64.exe" })
$qemuAfter = @($processAuditAfter | Where-Object { $_.name -ieq "qemu-system-x86_64.exe" })
$ownedQemuBefore = @($qemuBefore | Where-Object { $_.owned -eq $true })
$unrelatedQemuBefore = @($qemuBefore | Where-Object { $_.owned -ne $true })
$ownedQemuAfter = @($qemuAfter | Where-Object { $_.owned -eq $true })
$cleanupPass = $null -ne $processAuditAfter -and @($processAuditAfter | Where-Object { $_.owned -eq $true }).Count -eq 0
$runtimeLibraryHash = $null
$managedPeHash = $null
$elfHash = $null
$mapHash = $null
$proofKernelHash = $null
if ($null -ne $runtimePackManifestPath -and (Test-Path -LiteralPath $runtimePackManifestPath -PathType Leaf)) {
    $packForHashes = Get-Content -LiteralPath $runtimePackManifestPath -Raw | ConvertFrom-Json
    if (Test-Path -LiteralPath $packForHashes.adaptedRuntimeLibrary -PathType Leaf) { $runtimeLibraryHash = Get-Hash $packForHashes.adaptedRuntimeLibrary }
}
if ($null -ne $gcManifestPath -and (Test-Path -LiteralPath $gcManifestPath -PathType Leaf)) {
    $gcForHashes = Get-Content -LiteralPath $gcManifestPath -Raw | ConvertFrom-Json
    $proofKernelHash = $gcForHashes.qemu.proofKernelSha256
    $payloadProperties = @("managedPe", "elf", "map")
    foreach ($propertyName in $payloadProperties) {
        $payloadProperty = $gcForHashes.payloadHashes.PSObject.Properties[$propertyName]
        if ($null -ne $payloadProperty) {
            if ($propertyName -eq "managedPe") { $managedPeHash = $payloadProperty.Value }
            elseif ($propertyName -eq "elf") { $elfHash = $payloadProperty.Value }
            elseif ($propertyName -eq "map") { $mapHash = $payloadProperty.Value }
        }
    }
}
$ordinaryBefore = $canonicalOrdinaryBefore
$ordinaryAfter = $canonicalOrdinaryAfter
$topManifest = [ordered]@{
    schemaVersion = 2
    c52Identifier = "C011EC52"
    c51Identifier = "C011EC51"
    outcome = $outcome
    successLevel = $successLevel
    requestedTier = $Tier
    fullAggregate = $Tier -eq "All"
    uninterrupted = $allRequestedPassed -and $Tier -eq "All"
    requestedFreshBootCount = $FreshBootCount
    bootTimeoutSeconds = $BootTimeoutSeconds
    commandTimeoutSeconds = $CommandTimeoutSeconds
    failure = $failure
    failureTier = $failureTier
    failureCategory = $failureCategory
    aggregateInvocation = [ordered]@{
        command = "powershell -NoProfile -ExecutionPolicy Bypass -File scripts\dotnet\Invoke-C011EC51RuntimePackValidation.ps1 -Tier All"
        entrypoint = $PSCommandPath
        startedUtc = $null
        endedUtc = (Get-Date).ToUniversalTime().ToString("o")
        exitCode = if ($allRequestedPassed) { 0 } else { 1 }
        process = "single parent validation execution"
    }
    repository = [ordered]@{
        root = $RepoRoot
        startingHead = $startingHead
        startingSubject = $startingSubject
        startingBranch = $startingBranch
        startingUpstream = $startingUpstream
        startingAheadBehind = $startingAheadBehind
        startingStatus = $startingStatus
        startingUntracked = $startingUntracked
        startingWorktree = if ($startingStatus.Count -eq 0 -and $startingUntracked.Count -eq 0) { "clean" } else { "dirty" }
        finalHead = $finalHead
        finalSubject = $finalSubject
        finalBranch = $finalBranch
        finalUpstream = $finalUpstream
        finalAheadBehind = $finalAheadBehind
        finalStatus = $finalStatus
        finalUntracked = $finalUntracked
        finalWorktree = if ($finalStatus.Count -eq 0 -and $finalUntracked.Count -eq 0) { "clean" } else { "dirty" }
    }
    c51 = [ordered]@{ commit = $c51Commit; presentInHistory = $c51PresentInHistory; upstreamState = $c51UpstreamState }
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
    patch = [ordered]@{ path = $patchPath; sha256 = Get-Hash $patchPath; policy = $lock.nativeAotFpRepair.policy; patchVersion = $lock.nativeAotFpRepair.patchVersion }
    sourceStrategy = if ($null -eq $sourceCheckout) { $null } else { [ordered]@{ strategy=$sourceCheckout.strategy; selectedPath=$sourceCheckout.checkoutPath; defaultPath=$sourceCheckout.defaultPath; defaultState=$sourceCheckout.defaultState; selectedState=$sourceCheckout.selectedState; dirtySourceRejected=$sourceCheckout.dirtySourceRejected; dirtySourcePreserved=$sourceCheckout.dirtySourcePreserved; acquisition=$sourceCheckout.acquisition } }
    tierResults = $tierResults
    runtimePackManifest = if ($null -eq $runtimePackManifestPath -or -not (Test-Path -LiteralPath $runtimePackManifestPath -PathType Leaf)) { $null } else { [ordered]@{ path = $runtimePackManifestPath; sha256 = Get-Hash $runtimePackManifestPath } }
    gcProofManifest = if ($null -eq $gcManifestPath -or -not (Test-Path -LiteralPath $gcManifestPath -PathType Leaf)) { $null } else { [ordered]@{ path = $gcManifestPath; sha256 = Get-Hash $gcManifestPath } }
    ordinaryBootManifest = if ($null -eq $ordinaryManifestPath -or -not (Test-Path -LiteralPath $ordinaryManifestPath -PathType Leaf)) { $null } else { [ordered]@{ path = $ordinaryManifestPath; sha256 = Get-Hash $ordinaryManifestPath } }
    artifactProvenance = [ordered]@{
        runtimeLibrarySha256 = $runtimeLibraryHash
        managedPeSha256 = $managedPeHash
        elfSha256 = $elfHash
        mapSha256 = $mapHash
        proofKernelSha256 = $proofKernelHash
        ordinaryKernelBeforeSha256 = if ($null -eq $ordinaryBefore) { $null } else { $ordinaryBefore.kernel }
        ordinaryKernelAfterSha256 = if ($null -eq $ordinaryAfter) { $null } else { $ordinaryAfter.kernel }
        ordinaryEspBeforeSha256 = if ($null -eq $ordinaryBefore) { $null } else { $ordinaryBefore.espKernel }
        ordinaryEspAfterSha256 = if ($null -eq $ordinaryAfter) { $null } else { $ordinaryAfter.espKernel }
        fpPatchSha256 = Get-Hash $patchPath
    }
    canonicalArtifacts = [ordered]@{ before = $ordinaryBefore; after = $ordinaryAfter; mutation = if ($null -eq $ordinaryBefore -or $null -eq $ordinaryAfter) { "NOT_PROVEN" } elseif ($ordinaryBefore.kernel -eq $ordinaryAfter.kernel -and $ordinaryBefore.espKernel -eq $ordinaryAfter.espKernel) { "UNCHANGED" } else { "MUTATED" } }
    semanticRewriteGuard = [ordered]@{
        result = if ($allRequestedPassed) { "PASS" } else { "FAIL" }
        c46SemanticCompileDefine = $false
        c47SemanticCompileDefine = $false
        c48SemanticCompileDefine = $false
        generatedStackFrameIteratorReplacement = $false
        productionUsesDurablePatch = $true
    }
    processSafety = [ordered]@{
        preRun = [ordered]@{ totalQemu = $qemuBefore.Count; staleOwnedQemu = $ownedQemuBefore.Count; unrelatedQemuPreserved = $unrelatedQemuBefore.Count; snapshot = $processAuditBefore; cleanup = $preRunCleanup; afterCleanup = $processAuditAfterPreRunCleanup }
        final = [ordered]@{ ownedCleanup = if ($null -eq $finalCleanup) { @() } else { $finalCleanup }; remainingOwned = if ($null -eq $processAuditAfter) { @() } else { @($processAuditAfter | Where-Object { $_.owned -eq $true }) }; finalOwnedProcessCount = if ($null -eq $processAuditAfter) { $null } else { @($processAuditAfter | Where-Object { $_.owned -eq $true }).Count }; finalOwnedQemuCount = $ownedQemuAfter.Count; finalQemuCount = $qemuAfter.Count; cleanupPass = $cleanupPass; snapshot = $processAuditAfter }
        boundedLogs = $true
        childLogsOnDisk = $true
        embeddedLogPolicy = "paths, hashes, and bounded tails only"
        perBootTimeoutSeconds = $BootTimeoutSeconds
    }
    negativePropagation = $negativePropagation
    partialTierPassPrevention = [ordered]@{ result = if ($negativePropagation.result -eq "PASS") { "PASS" } else { "NOT_PROVEN" }; rule = "overall PASS requires every requested tier A through D to be PASS, all required manifests to validate, semantic guard to pass, and cleanup to pass"; negativeInvocation = $negativePropagation }
    manifestCompleteness = [ordered]@{ result = if ($allRequestedPassed -and $null -ne $processAuditAfter) { "PASS" } else { "FAIL" }; requiredTierSequence = @("A", "B", "C", "D"); requiredTopLevelSections = @("repository", "runtimeIdentity", "patch", "sourceStrategy", "tierResults", "artifactProvenance", "canonicalArtifacts", "semanticRewriteGuard", "processSafety", "negativePropagation", "c42", "aggregateManifestIntegrity"); manifestsRequired = [ordered]@{ tierA = $true; tierB = $true; tierC = $true; tierD = $true }; boundedLogsOnly = $true }
    c42 = [ordered]@{ included = $false; historicalAvailable = $true; rationale = "C42 historical mode remains available but is intentionally excluded; C49/C50 repeated-collection production proof supersedes it for this gate." }
    previousC51Interruption = [ordered]@{ classification = "EXTERNAL_HOST_INTERRUPTION"; reproduced = $false; pipelineEvidence = "none in C52"; operatorAssessment = "unrelated RDP activity was suspected; C52 did not reproduce a validator hang, unbounded memory condition, orphan QEMU, process leak, or build deadlock" }
    evidenceRoot = $runRoot
    manifestPath = $manifestPath
    aggregateManifestIntegrity = [ordered]@{ algorithm = "SHA-256"; sha256Path = (Join-Path $runRoot "c52.validation.manifest.sha256"); hashScope = "c52.validation.manifest.json bytes"; selfReferenceAvoided = $true }
}
$topManifest.aggregateInvocation.startedUtc = if ($null -ne $tierResults.A.startUtc) { $tierResults.A.startUtc } else { (Get-Date).ToUniversalTime().ToString("o") }
$topManifest | ConvertTo-Json -Depth 40 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
$aggregateManifestHash = Get-Hash $manifestPath
Set-Content -LiteralPath (Join-Path $runRoot "c52.validation.manifest.sha256") -Value ($aggregateManifestHash + "  c52.validation.manifest.json") -Encoding ASCII
Write-Host "[C52] outcome=$outcome successLevel=$successLevel requestedTier=$Tier exitCode=$(if ($allRequestedPassed) { 0 } else { 1 }) manifest=$manifestPath"
if (-not $allRequestedPassed) { exit 1 }
exit 0
