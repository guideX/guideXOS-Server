param(
    [string]$LogDirectory,
    [int]$Port = 8096
)

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false
$Root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $Root

if ([string]::IsNullOrWhiteSpace($LogDirectory)) {
    $LogDirectory = $env:GXOS_NAVIGATOR_SMOKE_LOG_DIR
}
if ([string]::IsNullOrWhiteSpace($LogDirectory)) {
    $LogDirectory = Join-Path $env:LOCALAPPDATA "Temp\guidex-js110-evidence\hosted"
}
$LogDirectory = [System.IO.Path]::GetFullPath($LogDirectory)
New-Item -ItemType Directory -Force -Path $LogDirectory | Out-Null
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$evidence = Join-Path $LogDirectory "js110-hosted-$stamp.txt"
$exe = Join-Path $Root "guideXOSServer.exe"
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "Fresh production executable is missing: $exe"
}
$exeHash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
$fixturePath = "navigator-smoke/javascript-js110.html"
$fixtureHash = (Get-FileHash -LiteralPath $fixturePath -Algorithm SHA256).Hash
$url = "http://127.0.0.1:$Port/$fixturePath"

$python = if (Test-Path -LiteralPath `
        "C:\Users\guideX\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe") {
    "C:\Users\guideX\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
} else {
    (Get-Command python -ErrorAction Stop).Source
}
$httpServer = Join-Path $Root "scripts\navigator_kernel_http_server.py"
$httpLog = Join-Path $LogDirectory "js110-http-$stamp.log"
$httpErrLog = Join-Path $LogDirectory "js110-http-$stamp.err.log"
$serverArgs = @(
    "`"$httpServer`"", "--port", "$Port", "--host", "127.0.0.1",
    "--root", "`"$Root`""
)
$httpProc = $null
$appProc = $null
$output = ""

try {
    $httpProc = Start-Process -FilePath $python -ArgumentList $serverArgs `
        -PassThru -WindowStyle Hidden -RedirectStandardOutput $httpLog `
        -RedirectStandardError $httpErrLog
    $ready = $false
    for ($attempt = 0; $attempt -lt 30; ++$attempt) {
        if ($httpProc.HasExited) {
            throw "JS110 local HTTP server exited early; see $httpErrLog"
        }
        try {
            $response = Invoke-WebRequest -Uri $url -UseBasicParsing -TimeoutSec 2
            if ($response.StatusCode -eq 200 -and
                $response.Content.Contains("Navigator JavaScript JS110")) {
                $ready = $true
                break
            }
        } catch {
            Start-Sleep -Milliseconds 250
        }
    }
    if (-not $ready) { throw "JS110 local page did not become available: $url" }

    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = "cmd.exe"
    $startInfo.Arguments = "/c (echo navigator.goto $url&& echo exit) | `"$exe`""
    $startInfo.WorkingDirectory = $Root
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.RedirectStandardInput = $true
    $appProc = New-Object System.Diagnostics.Process
    $appProc.StartInfo = $startInfo
    [void]$appProc.Start()
    $stdoutTask = $appProc.StandardOutput.ReadToEndAsync()
    $stderrTask = $appProc.StandardError.ReadToEndAsync()
    $appProc.StandardInput.Close()
    if (-not $appProc.WaitForExit(90000)) {
        $appProc.Kill()
        throw "Hosted JS110 page navigation exceeded the 90-second bound."
    }
    $output = $stdoutTask.GetAwaiter().GetResult() + "`n" +
        $stderrTask.GetAwaiter().GetResult()
    $output | Set-Content -LiteralPath $evidence
    if ($appProc.ExitCode -ne 0) {
        throw "Hosted Navigator process exited with code $($appProc.ExitCode); see $evidence"
    }
    if ($output -notmatch "(?m)^NAVIGATOR_GOTO_RESULT: PASS\r?$") {
        throw "Hosted Navigator did not report a successful JS110 page load; see $evidence"
    }
    if ($output -notmatch "(?m)^http_status=200\r?$") {
        throw "Hosted JS110 page did not receive HTTP 200; see $evidence"
    }
    if ($output -notmatch "(?m)^current_url=$([regex]::Escape($url))[ \t]*\r?$") {
        throw "Hosted Navigator did not retain the requested JS110 URL; see $evidence"
    }
    if ($output -notmatch "(?m)^javascript_error=\r?$") {
        throw "Hosted JS110 inline script reported a JavaScript runtime error; see $evidence"
    }

    @(
        "Executable SHA256: $exeHash"
        "JS110 fixture SHA256: $fixtureHash"
        "Requested URL: $url"
        "Hosted JS110 page load: PASS"
        "Hosted JS110 inline JavaScript error: none"
    ) | Add-Content -LiteralPath $evidence
    Write-Output "Hosted JS110 Navigator page PASS"
    Write-Output "Hosted executable SHA256: $exeHash"
    Write-Output "Hosted JS110 evidence: $evidence"
    Write-Output $output
} finally {
    if ($appProc -and -not $appProc.HasExited) {
        Stop-Process -Id $appProc.Id -Force -ErrorAction SilentlyContinue
    }
    if ($httpProc -and -not $httpProc.HasExited) {
        Stop-Process -Id $httpProc.Id -Force -ErrorAction SilentlyContinue
    }
}
