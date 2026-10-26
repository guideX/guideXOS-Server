$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') {
    throw 'C188R11R4 QAC stream capture tests require PowerShell Core.'
}

$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('c188-r11r4-qac-stream-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $testRoot
$probeScriptPath = Join-Path $testRoot 'probe.ps1'
$stdoutPath = Join-Path $testRoot 'stdout.log'
$stderrPath = Join-Path $testRoot 'stderr.log'
$process = $null
$capture = $null
try {
    $captureHelper = Join-Path $PSScriptRoot 'C188LiveProcessCapture.cs'
    Add-Type -Path $captureHelper
    $pwsh = Get-Command pwsh.exe -ErrorAction Stop | Select-Object -First 1
    @(
        "[Console]::Out.WriteLine('stdout-probe')"
        "[Console]::Error.WriteLine('stderr-probe')"
        'Start-Sleep -Milliseconds 1500'
    ) | Set-Content -LiteralPath $probeScriptPath -Encoding utf8
    $capture = [GuideXos.C188.LiveProcessCapture]::new(
        $pwsh.Source,$testRoot,[string[]]@('-NoProfile','-File',$probeScriptPath),$stdoutPath,$stderrPath)
    $process = $capture.NativeProcess
    $deadline = (Get-Date).AddSeconds(3)
    do {
        Start-Sleep -Milliseconds 50
        $stdout = if (Test-Path -LiteralPath $stdoutPath) { [GuideXos.C188.LiveProcessCapture]::ReadAllTextShared($stdoutPath) } else { '' }
        $stderr = if (Test-Path -LiteralPath $stderrPath) { [GuideXos.C188.LiveProcessCapture]::ReadAllTextShared($stderrPath) } else { '' }
        if ($stdout -match 'stdout-probe' -and $stderr -match 'stderr-probe') { break }
    } while (-not $process.HasExited -and (Get-Date) -lt $deadline)
    if ($process.HasExited) { throw 'The live-capture probe exited before its capture assertion.' }
    if ($stdout -notmatch 'stdout-probe') { throw 'QAC stdout redirection did not write its live line.' }
    if ($stderr -notmatch 'stderr-probe') { throw 'QAC stderr redirection did not write its live line.' }
    if (-not $process.WaitForExit(10000)) { throw 'The output redirection probe did not exit within its bound.' }
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) { throw "The stream-capture probe exited with code $($process.ExitCode)." }
    if (-not [string]::IsNullOrWhiteSpace($capture.CaptureError)) {
        throw "QAC stream capture reported an error: $($capture.CaptureError)"
    }
} finally {
    if ($capture) { $capture.Dispose() }
    elseif ($process) { $process.Dispose() }
    foreach ($path in @($probeScriptPath,$stdoutPath,$stderrPath)) {
        try {
            if (Test-Path -LiteralPath $path -PathType Leaf) { [System.IO.File]::Delete($path) }
        } catch { }
    }
    try {
        if ([System.IO.Directory]::Exists($testRoot)) { [System.IO.Directory]::Delete($testRoot) }
    } catch {
    }
}

Write-Output 'C188R11R4-QAC-STREAM-CAPTURE cases=2 result=PASS'
