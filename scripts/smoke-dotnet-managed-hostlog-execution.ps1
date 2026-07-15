param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$OutputRoot = "",
    [string]$StageRoot = "",
    [string]$StageScript = "",
    [string]$ServerExe = "",
    [string]$RuntimePackRoot = "",
    [ValidateSet("NonAllocating", "Allocating")]
    [string]$AllocationMode = "NonAllocating",
    [switch]$UseGuideXosRuntimePack,
    [int]$TimeoutSeconds = 240,
    [switch]$SkipFailureProbe,
    [switch]$SkipBuild
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

function Assert-RegexCountExactly {
    param(
        [string]$Text,
        [string]$Pattern,
        [int]$Expected,
        [string]$Reason
    )

    $normalizedText = $Text -replace "`r", ""
    $count = [regex]::Matches($normalizedText, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Multiline).Count
    if ($count -ne $Expected) {
        throw "Expected exactly $Expected matches for ${Reason}, but saw $count. Pattern: $Pattern"
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
if ($UseGuideXosRuntimePack -and [string]::IsNullOrWhiteSpace($RuntimePackRoot)) {
    $RuntimePackRoot = Join-Path $Root "tools\dotnet\runtime-pack"
}

$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$StageRoot = [System.IO.Path]::GetFullPath($StageRoot)
$StageScript = [System.IO.Path]::GetFullPath($StageScript)
$ServerExe = [System.IO.Path]::GetFullPath($ServerExe)
$expectedMessage = if ($AllocationMode -eq "Allocating") { "Hello from managed heap" } else { "Hello from managed guideXOS code" }

if (-not (Test-Path -LiteralPath $StageScript)) {
    throw "Stage script not found: $StageScript"
}

$mingwBin = "C:\mingw64\bin"
$oldPath = $env:PATH
$originalStageRoot = $env:GXOS_NATIVE_ELF_STAGE_ROOT
if (Test-Path -LiteralPath $mingwBin) {
    $env:PATH = "$mingwBin;$env:PATH"
}

try {
    $buildServerScript = Join-Path $Root "build-native-experimental.bat"
    if (-not (Test-Path -LiteralPath $buildServerScript)) {
        throw "Experimental server build script not found: $buildServerScript"
    }
    $buildText = Get-Content -LiteralPath $buildServerScript -Raw
    if ($buildText -notmatch 'GX_ENABLE_EXPERIMENTAL_NATIVE_ELF_EXECUTION') {
        throw "Experimental build script does not enable native ELF execution."
    }

    if (-not (Test-Path -LiteralPath $ServerExe)) {
        Write-Host "[dotnet-proof] experimental server missing, building it now"
        & cmd.exe /c "`"$buildServerScript`""
        if ($LASTEXITCODE -ne 0) {
            throw "Experimental server build failed with exit code $LASTEXITCODE"
        }
    }

    if (-not (Test-Path -LiteralPath $ServerExe)) {
        throw "Experimental server executable not found: $ServerExe"
    }

    Write-Host "[dotnet-proof] staging proof artifacts"
    $stageArguments = @(
        "-RepoRoot", $Root,
        "-OutputRoot", $OutputRoot,
        "-StageRoot", $StageRoot
    )
    if ($UseGuideXosRuntimePack) {
        $stageArguments += @("-RuntimePackRoot", $RuntimePackRoot, "-UseGuideXosRuntimePack")
    }
    $stageArguments += @("-AllocationMode", $AllocationMode)
    if ($SkipBuild) { $stageArguments += "-SkipBuild" }
    $stageOutput = & powershell -ExecutionPolicy Bypass -File $StageScript @stageArguments
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
    if ([string]$stageEnvelope.registrySourceEnvironment -ne "GXOS_NATIVE_ELF_STAGE_ROOT") {
        throw "Stage envelope does not identify the process-local stage-root environment."
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
    $expectedEntryCategory = if ($UseGuideXosRuntimePack) { "guidexos-runtime-pack-reverse-pinvoke" } else { "runtime-correct-reverse-pinvoke-required" }
    if ($stageManifestObject.entries[0].entryCategory -ne $expectedEntryCategory) {
        throw "Entry-category drift: expected $expectedEntryCategory."
    }
    if ($stageManifestObject.entries[0].abi -ne "guidexos-c-abi-v1") {
        throw "Missing expected value for stage manifest ABI: guidexos-c-abi-v1"
    }
    $expectedEntryFields = if ($UseGuideXosRuntimePack) {
        @{
            entryCategory = "guidexos-runtime-pack-reverse-pinvoke"
            converterSha256 = "5F21B87D343106120EB5CAD1F98DF524404171E084C40F4FC3AFED6BE6F84B96"
            ilCompilerPackage = "Microsoft.DotNet.ILCompiler"
            ilCompilerVersion = "9.0.0"
            runtimePackPackage = "runtime.win-x64.microsoft.dotnet.ilcompiler"
            runtimePackVersion = "9.0.0"
            useGuideXosRuntimePack = "True"
            tlsEnvelope = "guideXOS TLS-template-backed per-thread runtime cell with local FLS namespace"
        }
    } else {
        @{
            entryCategory = "runtime-correct-reverse-pinvoke-required"
            managedMainAddress = "0x10001900"
            reversePInvokeAddress = "0x1004B140"
            reversePInvokeAttachAddress = "0x1004B1A0"
            reversePInvokeReturnAddress = "0x1004B290"
            flsGetValueImportThunkAddress = "0x10052108"
            converterSha256 = "5F21B87D343106120EB5CAD1F98DF524404171E084C40F4FC3AFED6BE6F84B96"
            ilCompilerPackage = "Microsoft.DotNet.ILCompiler"
            ilCompilerVersion = "9.0.0"
            runtimePackPackage = "runtime.win-x64.microsoft.dotnet.ilcompiler"
            runtimePackVersion = "9.0.0"
            tlsEnvelope = "Windows TLS index plus NativeAOT TLS template envelope; FLS/thread attachment remains uninitialized"
        }
    }
    foreach ($field in $expectedEntryFields.Keys) {
        if ([string]$stageEnvelope.$field -ne $expectedEntryFields[$field]) {
            throw "Stage envelope drift for $field. Expected $($expectedEntryFields[$field]), got $($stageEnvelope.$field)."
        }
    }
    if ($UseGuideXosRuntimePack) {
        $expectedRuntimePackIdentity = if ($AllocationMode -eq "Allocating") { "guidexos-nativeaot-runtime-pack-amd64-hostlog-allocating-nocollection-v1" } else { "guidexos-nativeaot-runtime-pack-amd64-hostlog-nonallocating-v1" }
        if ([string]::IsNullOrWhiteSpace([string]$stageEnvelope.runtimePackIdentity) -or
            [string]$stageEnvelope.runtimePackIdentity -ne $expectedRuntimePackIdentity) {
            throw "Stage envelope does not identify the locked guideXOS runtime pack."
        }
        if ([string]::IsNullOrWhiteSpace([string]$stageEnvelope.runtimePackObjectSha256)) {
            throw "Stage envelope is missing the runtime-pack object hash."
        }
    }
    $toolchainFile = [string]$stageEnvelope.toolchainFile
    if (-not (Test-Path -LiteralPath $toolchainFile)) { throw "Toolchain provenance file missing: $toolchainFile" }
    Assert-Contains -Text (Get-Content -LiteralPath $toolchainFile -Raw) -Needle "PeToElfSha256=$($stageEnvelope.converterSha256)" -Reason "converter identity"

    $oldStageRoot = $originalStageRoot
    Set-EnvValue -Name "GXOS_NATIVE_ELF_STAGE_ROOT" -Value $null
    try {
        $preflightCommands = @(
            "nativeapp.inspect $appId"
        )
        $preflight = Invoke-ServerCommands -Label "preflight" -Commands $preflightCommands -WorkingDirectory $Root -ExePath $ServerExe
        if ($preflight.ExitCode -ne 0) {
            throw "Preflight server session failed with exit code $($preflight.ExitCode). stderr: $($preflight.StdErr)"
        }

        Assert-Contains -Text $preflight.StdOut -Needle "Result: app not found" -Reason "default inventory isolation"

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

        if ($env:GXOS_NATIVE_ELF_STAGE_ROOT -ne $oldStageRoot) {
            throw "Stage-root environment was not restored after the positive process."
        }

        $output = $positive.StdOut
        Write-Host $output

        Assert-Contains -Text $output -Needle "nativeapp.capabilities" -Reason "capabilities header"
        Assert-Contains -Text $output -Needle "experimental execution enabled: true" -Reason "experimental execution gate"
        Assert-Contains -Text $output -Needle "supported ELF type: static ET_EXEC" -Reason "supported ELF type"
        Assert-Contains -Text $output -Needle "supported ABI: guidexos-c-abi-v1" -Reason "supported ABI"
        Assert-RegexCountExactly -Text $output -Pattern '(?m)^executionSuccess:\s+true$' -Expected 2 -Reason "successful executions"
        Assert-RegexCountExactly -Text $output -Pattern '(?m)^executionAttempted:\s+true$' -Expected 2 -Reason "attempted executions"
        Assert-RegexCountExactly -Text $output -Pattern '(?m)^executionDiagnostics: .*Host log call count:\s+1' -Expected 2 -Reason "one host log call per launch"
        Assert-RegexCountExactly -Text $output -Pattern ("(?m)^executionDiagnostics: .*Last host log message:\s+" + [regex]::Escape($expectedMessage)) -Expected 2 -Reason "managed message in executor diagnostics"
        Assert-RegexCountExactly -Text $output -Pattern '(?m)^returnCode:\s+0$' -Expected 2 -Reason "return code"
        Assert-RegexCountExactly -Text $output -Pattern '(?m)^gxMainReturnCode:\s+0$' -Expected 2 -Reason "managed return code"
        Assert-RegexCountExactly -Text $output -Pattern '(?m)^trampolineUsed:\s+true$' -Expected 2 -Reason "trampoline use"
        Assert-RegexCountExactly -Text $output -Pattern '(?m)^entryHostAddress:\s+0x[0-9a-fA-F]+$' -Expected 2 -Reason "entry address"
        Assert-RegexCountExactly -Text $output -Pattern '(?m)^preferredBaseMappingAttempted:\s+true$' -Expected 2 -Reason "preferred-base attempts"
        Assert-RegexCountExactly -Text $output -Pattern '(?m)^preferredBaseMappingSuccess:\s+true$' -Expected 2 -Reason "preferred-base success"
        Assert-RegexCountExactly -Text $output -Pattern '(?m)^executionDiagnostics: Native ELF TLS bootstrap installed' -Expected 2 -Reason "TLS bootstrap success"
        Assert-RegexCountExactly -Text $output -Pattern '(?m)^executionDiagnostics: .*Preferred-base mapping:\s+success' -Expected 2 -Reason "preferred-base diagnostic"
        Assert-RegexCountExactly -Text $output -Pattern ("\[NativeAppHost\].*log: " + [regex]::Escape($expectedMessage)) -Expected 2 -Reason "host callback output"
        Assert-RegexCountExactly -Text $output -Pattern '(?m)^cleanupAttempted:\s+true$' -Expected 2 -Reason "cleanup"
        Assert-RegexCountExactly -Text $output -Pattern '(?m)^lifecycleStateAfterExecution:\s+Exited$' -Expected 2 -Reason "exit state"
        Assert-RegexCountAtLeast -Text $output -Pattern '(?m)^preferredBase:\s+0x10000000$' -Minimum 2 -Reason "preferred base"
        Assert-RegexCountExactly -Text $output -Pattern '(?m)^actualMappedBase:\s+0x10000000$' -Expected 2 -Reason "actual mapped base"
        if ($output -match '(?i)collision|Preferred-base allocation failure|TLS bootstrap failed') {
            throw "Live proof output contains a mapping/TLS failure or collision diagnostic."
        }

        $message = $expectedMessage
        $messageLines = @($output -replace "`r", "" -split "`n" | Where-Object { $_ -like "*$message*" })
        foreach ($line in $messageLines) {
            if ($line -notmatch ("^\[NativeAppHost\].*log: " + [regex]::Escape($expectedMessage) + '$') -and
                $line -notmatch ("Last host log message: " + [regex]::Escape($expectedMessage)) -and
                $line -notmatch ("executionDiagnostics: .*" + [regex]::Escape($expectedMessage))) {
                throw "Managed success message appeared through an unapproved output path: $line"
            }
        }

        $processPattern = "(?m)^runtimeId=(\d+)\s+appId=$([regex]::Escape($appId))\s+.*state=Exited.*$"
        $processMatches = [regex]::Matches(($output -replace "`r", ""), $processPattern)
        if ($processMatches.Count -ne 2) {
            throw "Expected exactly two exited process records for $appId, but saw $($processMatches.Count)."
        }

        $runtimeIds = @($processMatches | ForEach-Object { [uint64]$_.Groups[1].Value })
        if ($runtimeIds[0] -eq $runtimeIds[1]) { throw "Repeat launch reused the same runtimeId: $($runtimeIds[0])" }
        if ($runtimeIds[1] -le $runtimeIds[0]) { throw "Repeat launch runtimeIds did not increase monotonically: $($runtimeIds[0]) -> $($runtimeIds[1])" }

        if (-not $SkipFailureProbe) {
            $missingStageScript = Join-Path $OutputRoot "missing-stage-script.ps1"
            $failureArguments = @("-RepoRoot", $Root, "-OutputRoot", $OutputRoot, "-StageRoot", $StageRoot, "-StageScript", $missingStageScript, "-ServerExe", $ServerExe, "-SkipFailureProbe")
            if ($UseGuideXosRuntimePack) { $failureArguments += @("-RuntimePackRoot", $RuntimePackRoot, "-UseGuideXosRuntimePack") }
            & $powershell -ExecutionPolicy Bypass -File $PSCommandPath @failureArguments
            $failureProbeExitCode = $LASTEXITCODE
            if ($failureProbeExitCode -eq 0) { throw "Failure probe unexpectedly returned success." }
            Write-Host "[dotnet-proof] invalid-input failure probe returned nonzero: $failureProbeExitCode"
        }

        $messageLength = [System.Text.Encoding]::UTF8.GetByteCount($message)
        Write-Host "[dotnet-proof] launching NativeAOT managed entry"
        Write-Host "[dotnet-proof] artifact accepted"
        Write-Host "[dotnet-proof] host log invoked length=$messageLength"
        Write-Host "[dotnet-proof] managed method returned 0"
        Write-Host "[dotnet-proof] repeat launch runtimeIds=$($runtimeIds[0]),$($runtimeIds[1])"
        Write-Host "[dotnet-proof] stage proof root=$stageProofRoot"
        Write-Host "[dotnet-proof] stage app root=$stageAppRoot"
        Write-Host "[dotnet-proof] staged ELF hash=$stagedHash"
        Write-Host "[dotnet-proof] stage envelope=$stageEnvelopePath"

        Write-Host "NativeAOT managed-code execution proof PASS"
    } finally {
        Set-EnvValue -Name "GXOS_NATIVE_ELF_STAGE_ROOT" -Value $oldStageRoot
    }
} finally {
    $env:PATH = $oldPath
    Set-EnvValue -Name "GXOS_NATIVE_ELF_STAGE_ROOT" -Value $originalStageRoot
}


