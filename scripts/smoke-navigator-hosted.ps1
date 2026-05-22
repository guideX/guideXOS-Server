param(
    [switch]$Build
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$LogDir = Join-Path $Root "logs"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$log = Join-Path $LogDir "navigator-hosted-smoke-$stamp.log"

if ($Build) {
    & (Join-Path $Root "build.bat")
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$exe = Join-Path $Root "guideXOSServer.exe"
if (-not (Test-Path $exe)) {
    throw "guideXOSServer.exe not found. Run build.bat first or pass -Build."
}

$commands = @"
navigator.smoke
exit
"@

$output = $commands | & $exe 2>&1
$output | Tee-Object -FilePath $log

if ($output -match "NAVIGATOR_SMOKE_RESULT: PASS") {
    Write-Host "Hosted Navigator smoke PASS. Log: $log"
    exit 0
}

Write-Host "Hosted Navigator smoke FAIL. Log: $log" -ForegroundColor Red
exit 1
