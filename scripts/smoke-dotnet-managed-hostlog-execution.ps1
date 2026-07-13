param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$OutputRoot = "",
    [string]$StageRoot = "",
    [string]$StageScript = "",
    [string]$ServerExe = "",
    [int]$TimeoutSeconds = 240
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$Root = [System.IO.Path]::GetFullPath($RepoRoot)
. (Join-Path $Root "scripts\process_environment.ps1")
Normalize-ProcessEnvironment

$LogDir = Join-Path $Root "logs"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

function Set-EnvValue {
    param(
        [string]$Name,
        [string]$Value
    )

    if ($null -eq $Value) {
        Remove-Item "Env:$Name" -ErrorAction SilentlyContinue
    } else {
        Set-Item -Path "Env:$Name" -Value $Value
    }
}

function Assert-Contains {
    param(
        [string]$Text,
        [string]$Needle,
        [string]$Reason
    )

    $normalizedText = $Text -replace "`r", ""
    if ($normalizedText -notmatch [regex]::Escape($Needle)) {
        throw "Missing expected text for ${Reason}: $Needle"
    }
}

function Assert-RegexCountAtLeast {
    param(
        [string]$Text,
        [string]$Pattern,
        [int]$Minimum,
        [string]$Reason
    )

    $normalizedText = $Text -replace "`r", ""
    $count = [regex]::Matches($normalizedText, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Multiline).Count
    if ($count -lt $Minimum) {
        throw "Expected at least $Minimum matches for ${Reason}, but saw $count. Pattern: $Pattern"
    }
}

function Get-RegexValue([string]$Text, [string]$Pattern, [string]$Reason) {
    $normalizedText = $Text -replace "`r", ""
    $match = [regex]::Match($normalizedText, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Multiline)
    if (-not $match.Success) {
        throw "Missing expected capture for $Reason. Pattern: $Pattern"
    }
    return $match.Groups[1].Value
}

function Wait-ForProcessExit {
    param(
        [System.Diagnostics.Process]$Process,
        [int]$TimeoutSeconds
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while (-not $Process.HasExited -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 200
    }

    if (-not $Process.HasExited) {
        try { Stop-Process -Id $Process.Id -Force } catch { }
        throw "Process timed out after $TimeoutSeconds seconds."
    }

    $null = $Process.WaitForExit()
    $Process.Refresh()
}

function Invoke-ServerCommands {
    param(
        [string]$Label,
        [string[]]$Commands,
        [string]$WorkingDirectory,
        [string]$ExePath
    )

    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $input = Join-Path $LogDir "dotnet-managed-hostlog-$Label-$stamp.in"
    $stdoutLog = Join-Path $LogDir "dotnet-managed-hostlog-$Label-$stamp.log"
    $stderrLog = Join-Path $LogDir "dotnet-managed-hostlog-$Label-$stamp.err.log"

    ($Commands + @("exit")) -join [Environment]::NewLine | Set-Content -LiteralPath $input -Encoding ASCII

    try {
        Push-Location -LiteralPath $WorkingDirectory
        try {
            $cmdLine = "`"$ExePath`" < `"$input`" > `"$stdoutLog`" 2> `"$stderrLog`""
            & cmd.exe /c $cmdLine
            $exitCode = $LASTEXITCODE
        } finally {
            Pop-Location
        }
        $stdout = if (Test-Path -LiteralPath $stdoutLog) { Get-Content -LiteralPath $stdoutLog -Raw } else { "" }
        $stderr = if (Test-Path -LiteralPath $stderrLog) { Get-Content -LiteralPath $stderrLog -Raw } else { "" }
        return [pscustomobject]@{
            ExitCode = $exitCode
            StdOut = $stdout
            StdErr = $stderr
            StdOutLog = $stdoutLog
            StdErrLog = $stderrLog
        }
    } finally {
        Remove-Item -LiteralPath $input -ErrorAction SilentlyContinue
    }
}

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $Root "out\dotnet\managed-hostlog"
}
if ([string]::IsNullOrWhiteSpace($StageRoot)) {
    $StageRoot = Join-Path $OutputRoot "stage-managed-hostlog-proof"
}
if ([string]::IsNullOrWhiteSpace($StageScript)) {
    $StageScript = Join-Path $Root "scripts\dotnet\stage-managed-hostlog-proof.ps1"
}
if ([string]::IsNullOrWhiteSpace($ServerExe)) {
    $ServerExe = Join-Path $Root "guideXOSServer.experimental.exe"
}

$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$StageRoot = [System.IO.Path]::GetFullPath($StageRoot)
$StageScript = [System.IO.Path]::GetFullPath($StageScript)
$ServerExe = [System.IO.Path]::GetFullPath($ServerExe)

if (-not (Test-Path -LiteralPath $StageScript)) {
    throw "Stage script not found: $StageScript"
}

$mingwBin = "C:\mingw64\bin"
$oldPath = $env:PATH
if (Test-Path -LiteralPath $mingwBin) {
    $env:PATH = "$mingwBin;$env:PATH"
}

try {
    if (-not (Test-Path -LiteralPath $ServerExe)) {
        Write-Host "[dotnet-proof] experimental server missing, building it now"
        $buildServerScript = Join-Path $Root "build-native-experimental.bat"
        if (-not (Test-Path -LiteralPath $buildServerScript)) {
            throw "Experimental server build script not found: $buildServerScript"
        }

        & cmd.exe /c "`"$buildServerScript`""
        if ($LASTEXITCODE -ne 0) {
            throw "Experimental server build failed with exit code $LASTEXITCODE"
        }
    }

    if (-not (Test-Path -LiteralPath $ServerExe)) {
        throw "Experimental server executable not found: $ServerExe"
    }

    Write-Host "[dotnet-proof] staging proof artifacts"
    $stageOutput = & powershell -ExecutionPolicy Bypass -File $StageScript -RepoRoot $Root -OutputRoot $OutputRoot -StageRoot $StageRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Stage script failed with exit code $LASTEXITCODE"
    }

    $stageEnvelopePath = if ($stageOutput -is [array]) { [string]$stageOutput[-1] } else { [string]$stageOutput }
    if ([string]::IsNullOrWhiteSpace($stageEnvelopePath) -or -not (Test-Path -LiteralPath $stageEnvelopePath)) {
        throw "Stage envelope not found: $stageEnvelopePath"
    }

    $stageEnvelope = Get-Content -LiteralPath $stageEnvelopePath -Raw | ConvertFrom-Json
    $stageManifest = [string]$stageEnvelope.stageManifest
    $stageAppRoot = [string]$stageEnvelope.stageAppDirectory
    $stageAppsRoot = [string]$stageEnvelope.stageAppsRoot
    $stageProofRoot = [string]$stageEnvelope.stageProofRoot
    $stagedElf = [string]$stageEnvelope.stagedElf
    $sourceHash = [string]$stageEnvelope.sourceElfSha256
    $stagedHash = [string]$stageEnvelope.stagedElfSha256
    $appId = [string]$stageEnvelope.manifestId
    if ([string]::IsNullOrWhiteSpace($appId)) {
        $appId = "com.guidexos.experimental.nativeaot.hostlogproof"
    }

    if ($sourceHash -ne $stagedHash) {
        throw "Stage hash mismatch: source=$sourceHash staged=$stagedHash"
    }

    $stageManifestObject = Get-Content -LiteralPath $stageManifest -Raw | ConvertFrom-Json
    if ($stageManifestObject.kind -ne "NativeElf") {
        throw "Missing expected value for stage manifest kind: NativeElf"
    }
    if ($stageManifestObject.entries.Count -lt 1) {
        throw "Stage manifest does not contain any entries."
    }
    if ($stageManifestObject.entries[0].entryPoint -ne "ManagedMain") {
        throw "Missing expected value for stage manifest entry point: ManagedMain"
    }
    if ($stageManifestObject.entries[0].abi -ne "guidexos-c-abi-v1") {
        throw "Missing expected value for stage manifest ABI: guidexos-c-abi-v1"
    }

    $preflightCommands = @(
        "nativeapp.inspect $appId"
    )
    $preflight = Invoke-ServerCommands -Label "preflight" -Commands $preflightCommands -WorkingDirectory $Root -ExePath $ServerExe
    if ($preflight.ExitCode -ne 0) {
        throw "Preflight server session failed with exit code $($preflight.ExitCode). stderr: $($preflight.StdErr)"
    }

    Assert-Contains -Text $preflight.StdOut -Needle "Result: app not found" -Reason "default inventory isolation"

    $oldStageRoot = $env:GXOS_NATIVE_ELF_STAGE_ROOT
    Set-EnvValue -Name "GXOS_NATIVE_ELF_STAGE_ROOT" -Value $stageAppsRoot

    $positiveCommands = @(
        "nativeapp.capabilities",
        "nativeapp.smoketest $appId",
        "nativeapp.smoketest $appId",
        "nativeapp.processes"
    )
    $positive = $null
    try {
        $positive = Invoke-ServerCommands -Label "managed-hostlog" -Commands $positiveCommands -WorkingDirectory $Root -ExePath $ServerExe
    } finally {
        Set-EnvValue -Name "GXOS_NATIVE_ELF_STAGE_ROOT" -Value $oldStageRoot
    }

    if ($positive.ExitCode -ne 0) {
        throw "Managed proof server session failed with exit code $($positive.ExitCode). stderr: $($positive.StdErr)"
    }

    $output = $positive.StdOut
    Write-Host $output

    Assert-Contains -Text $output -Needle "nativeapp.capabilities" -Reason "capabilities header"
    Assert-Contains -Text $output -Needle "experimental execution enabled: true" -Reason "experimental execution gate"
    Assert-Contains -Text $output -Needle "supported ELF type: static ET_EXEC" -Reason "supported ELF type"
    Assert-Contains -Text $output -Needle "supported ABI: guidexos-c-abi-v1" -Reason "supported ABI"
    Assert-RegexCountAtLeast -Text $output -Pattern '(?m)^executionSuccess:\s+true$' -Minimum 2 -Reason "successful executions"
    Assert-RegexCountAtLeast -Text $output -Pattern '(?m)^executionAttempted:\s+true$' -Minimum 2 -Reason "attempted executions"
    Assert-RegexCountAtLeast -Text $output -Pattern 'Host log call count:\s+1' -Minimum 2 -Reason "host log count"
    Assert-RegexCountAtLeast -Text $output -Pattern 'Last host log message:\s+Hello from managed guideXOS code' -Minimum 2 -Reason "managed message"
    Assert-RegexCountAtLeast -Text $output -Pattern '(?m)^returnCode:\s+0$' -Minimum 2 -Reason "return code"
    Assert-RegexCountAtLeast -Text $output -Pattern '(?m)^gxMainReturnCode:\s+0$' -Minimum 2 -Reason "managed return code"
    Assert-RegexCountAtLeast -Text $output -Pattern '(?m)^trampolineUsed:\s+true$' -Minimum 2 -Reason "trampoline use"
    Assert-RegexCountAtLeast -Text $output -Pattern '(?m)^entryHostAddress:\s+0x10001900$' -Minimum 2 -Reason "entry address"
    Assert-RegexCountAtLeast -Text $output -Pattern '(?m)^cleanupAttempted:\s+true$' -Minimum 2 -Reason "cleanup"
    Assert-RegexCountAtLeast -Text $output -Pattern '(?m)^lifecycleStateAfterExecution:\s+Exited$' -Minimum 2 -Reason "exit state"

    $processPattern = "(?m)^runtimeId=(\d+)\s+appId=$([regex]::Escape($appId))\s+.*state=Exited.*$"
    $processMatches = [regex]::Matches($output, $processPattern)
    if ($processMatches.Count -lt 2) {
        throw "Expected at least two exited process records for $appId, but saw $($processMatches.Count)."
    }

    $runtimeIds = @($processMatches | ForEach-Object { [uint64]$_.Groups[1].Value })
    if ($runtimeIds[0] -eq $runtimeIds[1]) {
        throw "Repeat launch reused the same runtimeId: $($runtimeIds[0])"
    }
    if ($runtimeIds[1] -le $runtimeIds[0]) {
        throw "Repeat launch runtimeIds did not increase monotonically: $($runtimeIds[0]) -> $($runtimeIds[1])"
    }

    $message = "Hello from managed guideXOS code"
    $messageLength = [System.Text.Encoding]::UTF8.GetByteCount($message)
    Write-Host "[dotnet-proof] launching NativeAOT managed entry"
    Write-Host "[dotnet-proof] artifact accepted"
    Write-Host "[dotnet-proof] entry=0x10001900"
    Write-Host "[dotnet-proof] host log invoked length=$messageLength"
    Write-Host "[dotnet-proof] managed method returned 0"
    Write-Host "[dotnet-proof] repeat launch runtimeIds=$($runtimeIds[0]),$($runtimeIds[1])"
    Write-Host "[dotnet-proof] stage proof root=$stageProofRoot"
    Write-Host "[dotnet-proof] stage app root=$stageAppRoot"
    Write-Host "[dotnet-proof] staged ELF hash=$stagedHash"
    Write-Host "[dotnet-proof] stage envelope=$stageEnvelopePath"

    Write-Host "NativeAOT managed-code execution proof PASS"
} finally {
    $env:PATH = $oldPath
    Set-EnvValue -Name "GXOS_NATIVE_ELF_STAGE_ROOT" -Value $null
}


