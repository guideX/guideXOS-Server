param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$testRootBase = Join-Path $Root "tmp\startup-appmodel-regression-$stamp"
New-Item -ItemType Directory -Force -Path $testRootBase | Out-Null

function Assert-Check {
    param(
        [bool]$Condition,
        [string]$Message
    )

    if (-not $Condition) {
        throw "Startup App Model regression check failed: $Message"
    }
    Write-Host "PASS: $Message"
}

function New-ControlledRuntime {
    param([string]$Name)

    $path = Join-Path $testRootBase $Name
    New-Item -ItemType Directory -Force -Path $path | Out-Null
    foreach ($file in @("guideXOSServer.exe", "run-server.bat", "desktop.json", "desktop.state", "display-options.cfg")) {
        Copy-Item -LiteralPath (Join-Path $Root $file) -Destination (Join-Path $path $file)
    }

    # Start from a deliberately empty window list without touching the
    # repository's tracked desktop.json or any live runtime state.
    $config = Get-Content -LiteralPath (Join-Path $path "desktop.json") -Raw | ConvertFrom-Json
    $config.windows = @()
    $config.recent = @()
    $config | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $path "desktop.json") -Encoding UTF8
    "GXOSSTATE" | Set-Content -LiteralPath (Join-Path $path "desktop.state") -Encoding ASCII
    return $path
}

function Invoke-ControlledRuntime {
    param(
        [string]$Path,
        [string[]]$Commands,
        [string]$Label
    )

    $outputPath = Join-Path $Path ("$Label.output.log")
    $output = ($Commands | & (Join-Path $Path "run-server.bat") 2>&1 | Out-String)
    [System.IO.File]::WriteAllText($outputPath, $output)
    Write-Host "[$Label] output: $outputPath"
    return $output
}

function Get-WindowOwnershipBlock {
    param([string]$Output)

    $match = [regex]::Match($Output, "(?s)DESKTOP_WINDOW_OWNERS_BEGIN.*?DESKTOP_WINDOW_OWNERS_END")
    if (-not $match.Success) {
        throw "Window ownership diagnostic was not emitted."
    }
    return $match.Value
}

function Get-ConfigWindowCount {
    param([string]$Path)

    $config = Get-Content -LiteralPath (Join-Path $Path "desktop.json") -Raw | ConvertFrom-Json
    if ($null -eq $config.windows) { return 0 }
    return @($config.windows).Count
}

$buildScript = Get-Content -LiteralPath (Join-Path $Root "build.ps1") -Raw
$startupSmokeScript = Get-Content -LiteralPath (Join-Path $Root "scripts\smoke-desktop-startup-sync.ps1") -Raw
Assert-Check ($buildScript -notmatch '\$KernelExtraCFlags\s*\+=\s*"-DGXOS_DESKTOP_CLEANUP_RUNTIME_PASS') `
    "ordinary build.ps1 does not inject the boot-time launch-smoke flag"
Assert-Check ($buildScript.Contains('build\$Arch\obj\core\main.o')) `
    "ordinary build invalidates a stale launch-smoke main.o object"
Assert-Check ($startupSmokeScript.Contains('(-not $cleanupMarkerPresent)')) `
    "bare-metal startup smoke treats the launch-smoke marker as a failure"
Assert-Check ((Get-Content -LiteralPath (Join-Path $Root "kernel\core\main.cpp") -Raw).Contains('launch_app("DisplayOptions")')) `
    "explicit kernel launch smoke remains isolated behind its opt-in compile flag"

if (-not $SkipBuild) {
    Write-Host "Building hosted guideXOS Server..."
    & (Join-Path $Root "build.bat")
    if ($LASTEXITCODE -ne 0) { throw "Hosted build failed with exit code $LASTEXITCODE." }
}

$normalRoot = New-ControlledRuntime "normal"
$normalOutput = Invoke-ControlledRuntime -Path $normalRoot -Label "normal-startup" -Commands @(
    "desktop.apps",
    "gui.start",
    "desktop.windows.owners",
    "exit"
)
$normalOwnership = Get-WindowOwnershipBlock $normalOutput
Assert-Check ($normalOutput.Contains("gxos.builtin.notepad") -and
    $normalOutput.Contains("gxos.builtin.calculator") -and
    $normalOutput.Contains("gxos.builtin.displayoptions")) `
    "Notepad, Calculator, and Display Options are registered by canonical app ID"
Assert-Check ($normalOwnership.Contains("windowCount=0")) `
    "normal desktop startup creates no application windows"
Assert-Check (-not $normalOwnership.Contains("gxos.builtin.notepad") -and
    -not $normalOwnership.Contains("gxos.builtin.calculator") -and
    -not $normalOwnership.Contains("gxos.builtin.displayoptions")) `
    "registration does not launch any of the three applications"
Assert-Check (-not $normalOutput.Contains("STARTUP_APP_MODEL_REGRESSION_BEGIN")) `
    "the test-only launch diagnostic is not part of ordinary startup"

$explicitRoot = New-ControlledRuntime "explicit-launch"
$explicitOutput = Invoke-ControlledRuntime -Path $explicitRoot -Label "explicit-launch" -Commands @(
    "desktop.startup.regression",
    "exit"
)
foreach ($appId in @("gxos.builtin.notepad", "gxos.builtin.calculator", "gxos.builtin.displayoptions")) {
    Assert-Check ($explicitOutput.Contains("registered appId=$appId") -and
        $explicitOutput.Contains("explicitLaunch appId=$appId launchResult=PASS windowOwnership=PASS")) `
        "$appId is registered and explicitly launchable with canonical window ownership"
}
Assert-Check ($explicitOutput.Contains("registrationDidNotLaunch=PASS") -and
    $explicitOutput.Contains("STARTUP_APP_MODEL_REGRESSION_RESULT=PASS")) `
    "startup registration and explicit launch regression diagnostic passes"

$persistenceRoot = New-ControlledRuntime "persistence"
$firstOutput = Invoke-ControlledRuntime -Path $persistenceRoot -Label "persistence-first" -Commands @(
    "gui.start",
    "desktop.windows.owners",
    "exit"
)
$firstOwnership = Get-WindowOwnershipBlock $firstOutput
Assert-Check ($firstOwnership.Contains("windowCount=0") -and (Get-ConfigWindowCount $persistenceRoot) -eq 0) `
    "first launch leaves no application windows in persisted state"

$secondOutput = Invoke-ControlledRuntime -Path $persistenceRoot -Label "persistence-second" -Commands @(
    "gui.start",
    "desktop.windows.owners",
    "exit"
)
$secondOwnership = Get-WindowOwnershipBlock $secondOutput
Assert-Check ($secondOwnership.Contains("windowCount=0") -and (Get-ConfigWindowCount $persistenceRoot) -eq 0) `
    "second launch with the same state restores no unexpected application windows"

Write-Host "Startup App Model regression smoke PASS. Controlled artifacts: $testRootBase"
