param(
    [switch]$Build
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$LogDir = Join-Path $Root "logs"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
. (Join-Path $Root "scripts\process_environment.ps1")
Normalize-ProcessEnvironment
. (Join-Path $Root "scripts\navigator_smoke_repo_hygiene.ps1")

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$downloadsState = Save-NavigatorSmokeDirectoryState -LiteralPath (Join-Path $Root "downloads")

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

function Set-EnvFlag {
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

$python = Find-Python
if (-not $python) { throw "python not found; required for local Navigator POST smoke server." }

$httpLog = Join-Path $LogDir "navigator-hosted-http-$stamp.log"
$httpErrLog = Join-Path $LogDir "navigator-hosted-http-$stamp.err.log"
$httpServer = Join-Path $Root "scripts\navigator_kernel_http_server.py"
$httpArgs = @("`"$httpServer`"", "--port", "8080", "--https-port", "8443", "--host", "127.0.0.1", "--root", "`"$Root`"")
$httpProc = Start-Process -FilePath $python -ArgumentList $httpArgs -PassThru -WindowStyle Hidden -RedirectStandardOutput $httpLog -RedirectStandardError $httpErrLog
$httpsLog = Join-Path $LogDir "navigator-hosted-https-$stamp.log"
$httpsErrLog = Join-Path $LogDir "navigator-hosted-https-$stamp.err.log"
$tlsCert = Join-Path $Root "scripts\fixtures\navigator-smoke-localhost.crt"
$tlsKey = Join-Path $Root "scripts\fixtures\navigator-smoke-localhost.key"
$httpsArgs = @("`"$httpServer`"", "--port", "8443", "--host", "127.0.0.1", "--root", "`"$Root`"",
    "--http-port", "8080", "--tls-cert", "`"$tlsCert`"", "--tls-key", "`"$tlsKey`"")
$httpsProc = Start-Process -FilePath $python -ArgumentList $httpsArgs -PassThru -WindowStyle Hidden -RedirectStandardOutput $httpsLog -RedirectStandardError $httpsErrLog
Start-Sleep -Milliseconds 500
if ($httpProc.HasExited) {
    throw "local HTTP smoke server exited early; see $httpLog"
}
if ($httpsProc.HasExited) {
    throw "local HTTPS smoke server exited early; see $httpsLog"
}

$commands = @"
navigator.smoke
exit
"@
$input = Join-Path $LogDir "navigator-hosted-smoke-$stamp.in"
$log = Join-Path $LogDir "navigator-hosted-smoke-$stamp.log"
$err = Join-Path $LogDir "navigator-hosted-smoke-$stamp.err.log"
$commands | Set-Content -Path $input -Encoding ASCII

try {
    $oldTlsSmokeFlag = $env:GXOS_NAVIGATOR_SMOKE_ALLOW_SELF_SIGNED_LOCALHOST
    $oldExpectTrusted = $env:GXOS_NAVIGATOR_SMOKE_EXPECT_TRUSTED_LOCALHOST
    $oldExpectBypass = $env:GXOS_NAVIGATOR_SMOKE_EXPECT_SMOKE_LOCALHOST_BYPASS
    $oldDeferPaint = $env:GXOS_NAVIGATOR_SMOKE_DEFER_PAINT

    Set-EnvFlag -Name "GXOS_NAVIGATOR_SMOKE_ALLOW_SELF_SIGNED_LOCALHOST" -Value "1"
    Set-EnvFlag -Name "GXOS_NAVIGATOR_SMOKE_EXPECT_TRUSTED_LOCALHOST" -Value $null
    Set-EnvFlag -Name "GXOS_NAVIGATOR_SMOKE_EXPECT_SMOKE_LOCALHOST_BYPASS" -Value "1"
    Set-EnvFlag -Name "GXOS_NAVIGATOR_SMOKE_DEFER_PAINT" -Value "1"

    $appProc = Start-Process -FilePath $exe -PassThru -WindowStyle Hidden `
        -RedirectStandardInput $input -RedirectStandardOutput $log -RedirectStandardError $err
    Wait-Process -Id $appProc.Id
    $output = Get-Content $log -Raw
} finally {
    Set-EnvFlag -Name "GXOS_NAVIGATOR_SMOKE_ALLOW_SELF_SIGNED_LOCALHOST" -Value $oldTlsSmokeFlag
    Set-EnvFlag -Name "GXOS_NAVIGATOR_SMOKE_EXPECT_TRUSTED_LOCALHOST" -Value $oldExpectTrusted
    Set-EnvFlag -Name "GXOS_NAVIGATOR_SMOKE_EXPECT_SMOKE_LOCALHOST_BYPASS" -Value $oldExpectBypass
    Set-EnvFlag -Name "GXOS_NAVIGATOR_SMOKE_DEFER_PAINT" -Value $oldDeferPaint
    if ($httpProc -and -not $httpProc.HasExited) {
        Stop-Process -Id $httpProc.Id -Force
    }
    if ($httpsProc -and -not $httpsProc.HasExited) {
        Stop-Process -Id $httpsProc.Id -Force
    }
    Remove-Item $input -ErrorAction SilentlyContinue
    Restore-NavigatorSmokeDirectoryState -State $downloadsState
}

Write-Host $output

if ($output -match "NAVIGATOR_SMOKE_RESULT: PASS") {
    Write-Host "Hosted Navigator smoke PASS. Log: $log"
    exit 0
}

Write-Host "Hosted Navigator smoke FAIL. Log: $log" -ForegroundColor Red
exit 1
