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

function Find-Python {
    foreach ($candidate in @(
        "C:\Users\guideX\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe",
        "$Root\.venv\Scripts\python.exe"
    )) {
        if (Test-Path $candidate) { return $candidate }
    }
    $python = Get-Command "python" -ErrorAction SilentlyContinue
    if ($python) { return $python.Source }
    $py = Get-Command "py" -ErrorAction SilentlyContinue
    if ($py) { return $py.Source }
    return $null
}

$python = Find-Python
if (-not $python) { throw "python not found; required for local Navigator POST smoke server." }

$httpLog = Join-Path $LogDir "navigator-hosted-http-$stamp.log"
$httpErrLog = Join-Path $LogDir "navigator-hosted-http-$stamp.err.log"
$httpServer = Join-Path $Root "scripts\navigator_kernel_http_server.py"
$httpArgs = @("`"$httpServer`"", "--port", "8080", "--host", "127.0.0.1", "--root", "`"$Root`"")
$httpProc = Start-Process -FilePath $python -ArgumentList $httpArgs -PassThru -WindowStyle Hidden -RedirectStandardOutput $httpLog -RedirectStandardError $httpErrLog
Start-Sleep -Milliseconds 500
if ($httpProc.HasExited) {
    throw "local HTTP smoke server exited early; see $httpLog"
}

$commands = @"
navigator.smoke
exit
"@

try {
    $output = $commands | & $exe 2>&1
} finally {
    if ($httpProc -and -not $httpProc.HasExited) {
        Stop-Process -Id $httpProc.Id -Force
    }
}
$output | Tee-Object -FilePath $log

if ($output -match "NAVIGATOR_SMOKE_RESULT: PASS") {
    Write-Host "Hosted Navigator smoke PASS. Log: $log"
    exit 0
}

Write-Host "Hosted Navigator smoke FAIL. Log: $log" -ForegroundColor Red
exit 1
