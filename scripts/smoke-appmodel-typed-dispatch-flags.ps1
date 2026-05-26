param(
    [switch]$SkipNormalCheck
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$BuildBat = Join-Path $Root "build.bat"
$OutDir = Join-Path $Root "out\appmodel-typed-dispatch-flags"
$LogDir = Join-Path $Root "logs"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$TempBuildBat = Join-Path $OutDir "build-invalid-flags.bat"
$InvalidExeRelative = "out\appmodel-typed-dispatch-flags\guideXOSServer.invalid-flags.exe"
$InvalidExe = Join-Path $Root $InvalidExeRelative
$SmokeLog = Join-Path $LogDir "appmodel-typed-dispatch-flags-smoke-$stamp.log"

if (-not (Test-Path $BuildBat)) {
    throw "build.bat not found: $BuildBat"
}

$buildText = Get-Content $BuildBat -Raw
$buildText = $buildText -replace 'set CXXFLAGS=([^\r\n]*)', 'set CXXFLAGS=$1 -DGXOS_APPMODEL_TYPED_DISPATCH_SHADOW_ONLY -DGXOS_APPMODEL_TYPED_DISPATCH_ENABLED'
$buildText = $buildText -replace 'set OUTPUT=guideXOSServer\.exe', "set OUTPUT=$InvalidExeRelative"
Set-Content -Path $TempBuildBat -Value $buildText -Encoding ASCII

function Invoke-ServerCommands {
    param(
        [string]$ExePath,
        [string[]]$Commands
    )

    if (-not (Test-Path $ExePath)) {
        throw "Executable not found: $ExePath"
    }

    $inputText = (($Commands + @("exit")) -join [Environment]::NewLine) + [Environment]::NewLine
    return $inputText | & $ExePath
}

function Assert-Contains {
    param(
        [string]$Text,
        [string]$Needle,
        [string]$Reason
    )

    if (-not $Text.Contains($Needle)) {
        throw "Missing expected text for ${Reason}: $Needle"
    }
}

Push-Location $Root
try {
    Write-Host "Building temporary invalid typed-dispatch flag diagnostic binary..."
    & cmd.exe /c "`"$TempBuildBat`""
    if ($LASTEXITCODE -ne 0) {
        throw "Temporary invalid-flag build failed with exit code $LASTEXITCODE"
    }

    Write-Host "Running invalid-flag diagnostics..."
    $invalidOutput = Invoke-ServerCommands -ExePath $InvalidExe -Commands @(
        "desktop.appmodel.summary",
        "desktop.appmodel.typed-dispatch-gate"
    )

    Assert-Contains $invalidOutput "typedDispatchFlags: shadowOnly=ON enabled=ON behavior=legacy-dispatch status=WARN discoveryOnly=true invalidConfiguration=true" "summary invalid flag line"
    Assert-Contains $invalidOutput "check=typedDispatchCompileFlags status=WARN" "gate invalid flag status"
    Assert-Contains $invalidOutput "shadowOnly=ON enabled=ON behavior=legacy-dispatch discoveryOnly=true invalidConfiguration=true" "gate invalid flag detail"
    Assert-Contains $invalidOutput "enablesTypedDispatch: false" "gate remains report-only"
    Assert-Contains $invalidOutput "feedsTypedDispatchIntoLaunch: false" "typed dispatch is not fed into launch"

    $normalOutput = ""
    if (-not $SkipNormalCheck) {
        $NormalExe = Join-Path $Root "guideXOSServer.exe"
        Write-Host "Checking normal hosted binary still reports default flags..."
        $normalOutput = Invoke-ServerCommands -ExePath $NormalExe -Commands @(
            "desktop.appmodel.summary"
        )
        Assert-Contains $normalOutput "typedDispatchFlags: shadowOnly=OFF enabled=OFF behavior=legacy-dispatch status=OK discoveryOnly=true" "normal default flag line"
    }

    $report = @(
        "[AppModelTypedDispatchFlagsSmoke]",
        "mode=diagnostic-only",
        "temporaryBuildScript=$TempBuildBat",
        "temporaryExecutable=$InvalidExe",
        "permanentBuildFlagsChanged=false",
        "invalidSummaryLine=typedDispatchFlags: shadowOnly=ON enabled=ON behavior=legacy-dispatch status=WARN discoveryOnly=true invalidConfiguration=true",
        "invalidGateCheck=check=typedDispatchCompileFlags status=WARN",
        "launchBehavior=legacy-dispatch",
        "enablesTypedDispatch=false",
        "feedsTypedDispatchIntoLaunch=false",
        "normalDefaultChecked=$((-not $SkipNormalCheck).ToString().ToLowerInvariant())",
        "normalDefaultLine=typedDispatchFlags: shadowOnly=OFF enabled=OFF behavior=legacy-dispatch status=OK discoveryOnly=true",
        "result=PASS"
    ) -join [Environment]::NewLine

    Set-Content -Path $SmokeLog -Value ($report + [Environment]::NewLine + [Environment]::NewLine + "[invalid-output]" + [Environment]::NewLine + ($invalidOutput -join [Environment]::NewLine) + [Environment]::NewLine + "[normal-output]" + [Environment]::NewLine + ($normalOutput -join [Environment]::NewLine)) -Encoding ASCII
    Write-Host $report
    Write-Host "Smoke log: $SmokeLog"
    exit 0
} finally {
    Pop-Location
}
