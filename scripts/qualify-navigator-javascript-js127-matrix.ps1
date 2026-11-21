param(
    [string]$EvidenceDirectory,
    [string]$LaneFilter,
    [int]$TimeoutSeconds = 1800
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$ScriptRoot = Join-Path $Root "scripts"
Set-Location -LiteralPath $Root

if ($TimeoutSeconds -le 0) { throw "TimeoutSeconds must be positive." }
if ([string]::IsNullOrWhiteSpace($EvidenceDirectory)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $runId = [Guid]::NewGuid().ToString("N")
    $EvidenceDirectory = Join-Path `
        "E:\CodexArtifacts\guideXOSServer_NAVIGATOR_JS_SUPPORT\JS127" `
        ("matrix-$stamp-$runId")
}
$Evidence = [System.IO.Path]::GetFullPath($EvidenceDirectory)
if (Test-Path -LiteralPath $Evidence) {
    throw "Refusing to overwrite JS127 matrix evidence: $Evidence"
}
New-Item -ItemType Directory -Path $Evidence -Force | Out-Null

$PowerShellExe = Join-Path $PSHOME "pwsh.exe"
if (-not (Test-Path -LiteralPath $PowerShellExe -PathType Leaf)) {
    $PowerShellExe = Join-Path $PSHOME "powershell.exe"
}
if (-not (Test-Path -LiteralPath $PowerShellExe -PathType Leaf)) {
    throw "Could not locate the current PowerShell executable."
}

$scripts = @(
    Get-ChildItem -LiteralPath $ScriptRoot -File -Filter `
        "smoke-navigator-javascript-*.ps1" |
        Where-Object {
            $_.Name -match '^smoke-navigator-javascript-(lexer|parser|runtime|portable-execution|js\d+r?\d*)\.ps1$'
        } | ForEach-Object {
            $name = $_.BaseName -replace '^smoke-navigator-javascript-', ''
            if ($name -match '^js(\d+)(?:r(\d*))?$') {
                $revision = 0
                if ($Matches[2] -ne $null) {
                    $revision = if ($Matches[2] -eq "") { 1 } else {
                        [int]$Matches[2]
                    }
                }
                [pscustomobject]@{
                    Path = $_.FullName
                    Major = [int]$Matches[1]
                    Revision = $revision
                    Name = $_.Name
                }
            } else {
                [pscustomobject]@{
                    Path = $_.FullName
                    Major = -1
                    Revision = switch ($name) {
                        "lexer" { 0 }
                        "parser" { 1 }
                        "runtime" { 2 }
                        "portable-execution" { 3 }
                    }
                    Name = $_.Name
                }
            }
        } | Sort-Object Major, Revision, Name
)
if (-not [string]::IsNullOrWhiteSpace($LaneFilter)) {
    $scripts = @($scripts | Where-Object { $_.Name -match $LaneFilter })
}
if ($scripts.Count -eq 0) { throw "No matching JavaScript smoke lanes were found." }

$results = [System.Collections.Generic.List[object]]::new()
$assertionsPassed = 0
$assertionsTotal = 0
$assertionLanes = 0
for ($index = 0; $index -lt $scripts.Count; ++$index) {
    $lane = $scripts[$index]
    $slug = [System.IO.Path]::GetFileNameWithoutExtension($lane.Name)
    $laneDir = Join-Path $Evidence ("{0:D3}-{1}" -f ($index + 1), $slug)
    New-Item -ItemType Directory -Path $laneDir -Force | Out-Null
    $stdoutPath = Join-Path $laneDir "stdout.log"
    $stderrPath = Join-Path $laneDir "stderr.log"
    $metadataPath = Join-Path $laneDir "process-result.json"
    $stdout = ""
    $stderr = ""
    $exitCode = $null
    $launched = $false
    $timedOut = $false
    $status = "LaunchFailed"
    $durationMs = 0
    $process = [System.Diagnostics.Process]::new()
    $info = [System.Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $PowerShellExe
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.ArgumentList.Add("-NoLogo")
    $info.ArgumentList.Add("-NoProfile")
    $info.ArgumentList.Add("-NonInteractive")
    $info.ArgumentList.Add("-ExecutionPolicy")
    $info.ArgumentList.Add("Bypass")
    $info.ArgumentList.Add("-File")
    $info.ArgumentList.Add($lane.Path)
    $process.StartInfo = $info
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    try {
        $launched = $process.Start()
        if (-not $launched) { throw "Process.Start returned false." }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        $exited = $process.WaitForExit($TimeoutSeconds * 1000)
        if (-not $exited) {
            $timedOut = $true
            try { $process.Kill($true) } catch { }
            $process.WaitForExit()
        } else {
            $process.WaitForExit()
            $exitCode = [int]$process.ExitCode
            $status = if ($exitCode -eq 0) { "Completed" } else { "ProcessFailed" }
        }
        $stdout = $stdoutTask.GetAwaiter().GetResult()
        $stderr = $stderrTask.GetAwaiter().GetResult()
        if ($timedOut) { $status = "TimedOut" }
        elseif ($exitCode -eq 0 -and [string]::IsNullOrWhiteSpace($stdout)) {
            $status = "IndeterminateMissingOutput"
        }
    } catch {
        $stderr = ($stderr + [Environment]::NewLine + $_.ToString()).Trim()
        $status = if ($launched) { "ProcessCollectionFailed" } else { "LaunchFailed" }
    } finally {
        $timer.Stop()
        $durationMs = $timer.ElapsedMilliseconds
        $process.Dispose()
    }

    [System.IO.File]::WriteAllText($stdoutPath, $stdout,
        [System.Text.UTF8Encoding]::new($false))
    [System.IO.File]::WriteAllText($stderrPath, $stderr,
        [System.Text.UTF8Encoding]::new($false))
    $assertionCount = $false
    foreach ($line in ($stdout -split "`r?`n")) {
        if ($line -match 'Navigator JavaScript .*checks:\s*(\d+)/(\d+) passed') {
            $assertionsPassed += [int]$Matches[1]
            $assertionsTotal += [int]$Matches[2]
            $assertionCount = $true
        } elseif ($line -match 'Reported focused assertions:\s*(\d+)/(\d+)') {
            $assertionsPassed += [int]$Matches[1]
            $assertionsTotal += [int]$Matches[2]
            $assertionCount = $true
        }
    }
    if ($assertionCount) { ++$assertionLanes }
    $validExit = $launched -and -not $timedOut -and $null -ne $exitCode
    $success = $validExit -and $exitCode -eq 0 -and
        -not [string]::IsNullOrWhiteSpace($stdout)
    if ($success) { $status = "Passed" }
    $metadata = [pscustomobject]@{
        lane = $lane.Name
        script = $lane.Path
        launched = $launched
        timedOut = $timedOut
        exitCode = $exitCode
        exitCodeVerified = $validExit
        stdoutBytes = [System.Text.Encoding]::UTF8.GetByteCount($stdout)
        stderrBytes = [System.Text.Encoding]::UTF8.GetByteCount($stderr)
        assertionCountReported = $assertionCount
        durationMilliseconds = $durationMs
        status = $status
    }
    $metadata | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $metadataPath
    $results.Add($metadata)
    Write-Output ("[{0}/{1}] {2}: {3}, exit={4}, {5} ms" -f `
        ($index + 1), $scripts.Count, $lane.Name, $status, $exitCode, $durationMs)
    if ($stdout.Length -ne 0) { Write-Output $stdout.TrimEnd() }
    if ($stderr.Length -ne 0) { Write-Output "[stderr saved separately]" }
}

$passed = @($results | Where-Object { $_.status -eq "Passed" }).Count
$failed = $results.Count - $passed
$summary = [pscustomobject]@{
    status = if ($failed -eq 0) { "Passed" } else { "FailedOrIndeterminate" }
    laneCount = $results.Count
    passedLanes = $passed
    failedOrIndeterminateLanes = $failed
    assertionLanes = $assertionLanes
    reportedAssertionsPassed = $assertionsPassed
    reportedAssertionsTotal = $assertionsTotal
    processResultsVerified = (@($results | Where-Object {
        -not $_.exitCodeVerified
    }).Count -eq 0)
    timeoutSecondsPerLane = $TimeoutSeconds
    laneResults = $results
}
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath `
    (Join-Path $Evidence "matrix-summary.json")
@(
    "JavaScript matrix lanes: $passed/$($results.Count) passed.",
    "Reported assertions: $assertionsPassed/$assertionsTotal across $assertionLanes lanes.",
    "Process exit codes verified: $($summary.processResultsVerified).",
    "Evidence: $Evidence"
) | Set-Content -LiteralPath (Join-Path $Evidence "matrix-summary.txt")
Write-Output "JavaScript matrix lanes: $passed/$($results.Count) passed."
if ($assertionLanes -gt 0) {
    Write-Output "Reported assertions: $assertionsPassed/$assertionsTotal across $assertionLanes lanes."
}
Write-Output "Process exit codes verified: $($summary.processResultsVerified)."
Write-Output "Matrix evidence: $Evidence"
if ($failed -ne 0) { throw "Matrix has $failed failed or indeterminate lane(s)." }
