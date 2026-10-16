[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$ServerRoot,
    [Parameter(Mandatory = $true)] [string]$DeveloperStudioRepo,
    [Parameter(Mandatory = $true)] [string]$FixtureRoot,
    [Parameter(Mandatory = $true)] [string]$NativeMonitorPath,
    [Parameter(Mandatory = $true)] [string]$EvidenceRoot,
    [Parameter(Mandatory = $true)] [ValidatePattern('^[A-Za-z0-9-]+$')] [string]$RunToken
)

$ErrorActionPreference = 'Stop'
$ServerRoot = [IO.Path]::GetFullPath($ServerRoot)
$DeveloperStudioRepo = [IO.Path]::GetFullPath($DeveloperStudioRepo)
$FixtureRoot = [IO.Path]::GetFullPath($FixtureRoot)
$NativeMonitorPath = [IO.Path]::GetFullPath($NativeMonitorPath)
$EvidenceRoot = [IO.Path]::GetFullPath($EvidenceRoot)
if (Test-Path -LiteralPath $EvidenceRoot) { throw "Evidence root already exists; use a fresh run token and directory: $EvidenceRoot" }
New-Item -ItemType Directory -Path $EvidenceRoot | Out-Null

$serverExe = Join-Path $ServerRoot 'guideXOSServer.experimental.exe'
$smokeGate = Join-Path $DeveloperStudioRepo 'tests\smoke-developer-studio-debugger-positive-exit-gate.ps1'
$serverSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\server.cpp') -Raw
$lifecycleSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\lifecycle.cpp') -Raw
$serverHash = (Get-FileHash -LiteralPath $serverExe -Algorithm SHA256).Hash
foreach ($required in @($serverExe, $smokeGate, $FixtureRoot, $NativeMonitorPath)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Required shutdown-regression input missing: $required" }
}

$existing = @(Get-CimInstance -ClassName Win32_Process -Filter "Name='guideXOSServer.experimental.exe'" -ErrorAction SilentlyContinue |
    Where-Object { $_.ExecutablePath -and $_.ExecutablePath -ieq $serverExe })
if ($existing.Count -gt 0) { throw "Matching Server already runs before regression: $($existing.ProcessId -join ',')" }

$traceRoot = Join-Path $EvidenceRoot 'trace'
New-Item -ItemType Directory -Path $traceRoot | Out-Null
$monitorLog = Join-Path $EvidenceRoot 'native-debug-monitor.log'
$monitorOut = Join-Path $EvidenceRoot 'native-monitor.stdout.log'
$monitorErr = Join-Path $EvidenceRoot 'native-monitor.stderr.log'
$monitor = Start-Process -FilePath $NativeMonitorPath -ArgumentList @($serverExe, $EvidenceRoot, '900') -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput $monitorOut -RedirectStandardError $monitorErr
$readyDeadline = (Get-Date).AddSeconds(10)
while (-not (Test-Path -LiteralPath $monitorLog) -and (Get-Date) -lt $readyDeadline) { Start-Sleep -Milliseconds 50 }
if (-not (Test-Path -LiteralPath $monitorLog)) {
    Stop-Process -Id $monitor.Id -Force -ErrorAction SilentlyContinue
    throw "Native exception monitor did not arm for $RunToken"
}

$smokeOut = Join-Path $EvidenceRoot 'smoke.stdout.log'
$smokeErr = Join-Path $EvidenceRoot 'smoke.stderr.log'
$powerShellExe = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
$smokeArgs = @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $smokeGate,
    '-ServerRoot', $ServerRoot,
    '-FixtureRoot', $FixtureRoot,
    '-Iterations', '1',
    '-StartIteration', '1',
    '-ExpectedTargetExitCode', '0',
    '-MaxRuntimeSeconds', '240',
    '-DebugWaitSeconds', '120',
    '-TraceDirectory', $traceRoot
)
@{ run_token = $RunToken; server_executable = $serverExe; server_sha256 = $serverHash; smoke_arguments = $smokeArgs;
    fixture_source = $FixtureRoot; trace_root = $traceRoot; monitor_executable = $NativeMonitorPath } |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $EvidenceRoot 'command.json') -Encoding UTF8
& $powerShellExe @smokeArgs *> $smokeOut
$smokeExit = $LASTEXITCODE
if (-not $monitor.WaitForExit(15000)) {
    Stop-Process -Id $monitor.Id -Force -ErrorAction SilentlyContinue
    $monitor.WaitForExit()
    $monitorStuck = $true
} else {
    $monitorStuck = $false
}

$smokeText = if (Test-Path -LiteralPath $smokeOut) { Get-Content -LiteralPath $smokeOut -Raw } else { '' }
$monitorText = if (Test-Path -LiteralPath $monitorLog) { Get-Content -LiteralPath $monitorLog -Raw } else { '' }
$childTrace = Join-Path $traceRoot 'hosted-target-exit-001-child.log'
$traceText = if (Test-Path -LiteralPath $childTrace) { Get-Content -LiteralPath $childTrace -Raw } else { '' }
$serverPid = [regex]::Match($monitorText, '(?m)^TARGET_FOUND pid=(\d+)').Groups[1].Value
$smokeServerPid = [regex]::Match($smokeText, 'SERVER_PROCESS_START pid=(\d+)').Groups[1].Value
$directExit = [regex]::Match($monitorText, '(?m)^SERVER_HANDLE_EXIT .*$').Value
$monitorComplete = [regex]::Match($monitorText, '(?m)^MONITOR_COMPLETE .*$').Value
$avCount = ([regex]::Matches($monitorText, '(?m)^ACCESS_VIOLATION ')).Count
$remaining = @(Get-CimInstance -ClassName Win32_Process -Filter "Name='guideXOSServer.experimental.exe'" -ErrorAction SilentlyContinue |
    Where-Object { $_.ExecutablePath -and $_.ExecutablePath -ieq $serverExe })
$sourceShutdownContract =
    $serverSource.Contains('const bool shutdownComplete = Lifecycle::shutdown();') -and
    $serverSource.Contains('return shutdownComplete ? 0 : 1;') -and
    $lifecycleSource.Contains('NativeAppProcessTable::List()') -and
    $lifecycleSource.Contains('ipc::Bus::publish("gui.input", std::move(close), false)') -and
    $lifecycleSource.Contains('ProcessTable::wait(pid, kServiceShutdownTimeoutMs, &exitCode)') -and
    $lifecycleSource.Contains('ProcessTable::waitForThreadExit(pid, kServiceShutdownTimeoutMs)')
$failures = New-Object 'System.Collections.Generic.List[string]'
if ($smokeExit -ne 0) { $failures.Add("smoke_exit=$smokeExit") }
if (-not $smokeText.Contains('hosted_target_exit_iteration_001=PASS target_sessions=2 exit_code=0')) { $failures.Add('positive_gxsm_smoke_marker_missing') }
if (-not $serverPid -or $serverPid -ne $smokeServerPid) { $failures.Add('server_pid_identity_mismatch') }
if ($directExit -notmatch 'exit_code=0x0 .*read_result=PASS') { $failures.Add('direct_server_exit_not_zero') }
if ($monitorComplete -notmatch 'result=PASS return_code=0' -or $monitorStuck) { $failures.Add('native_monitor_completion_failed') }
if ($avCount -ne 0) { $failures.Add("access_violation_events=$avCount") }
if ($remaining.Count -ne 0) { $failures.Add('matching_server_process_remains=' + (($remaining | ForEach-Object { $_.ProcessId }) -join ',')) }
if (-not $sourceShutdownContract) { $failures.Add('server_source_shutdown_contract_missing') }
if (([regex]::Matches($traceText, 'TARGET_EXIT_NORMAL code=0')).Count -lt 2) { $failures.Add('target_exit_markers_incomplete') }
if (([regex]::Matches($traceText, 'debugger_teardown=PASS')).Count -lt 2) { $failures.Add('debugger_teardown_markers_incomplete') }
if (([regex]::Matches($traceText, 'GXSM validation .*present=1 valid=1 version=2 .*result=accepted')).Count -lt 2) { $failures.Add('gxsm_v2_metadata_markers_incomplete') }
if (([regex]::Matches($traceText, 'raw_bytes=02 00 00 00 raw=0x2 signed_value=2')).Count -lt 2) { $failures.Add('live_counter_two_reads_incomplete') }
if (([regex]::Matches($traceText, 'expression=counter\s*==\s*2 status=1 .*value=1 accepted=1')).Count -lt 2) { $failures.Add('true_watch_evaluations_incomplete') }
if (([regex]::Matches($traceText, 'debug_watch_runtime=invalidated')).Count -lt 2) { $failures.Add('watch_invalidation_markers_incomplete') }
$result = [pscustomobject]@{
    run_token = $RunToken
    server_executable = $serverExe
    server_sha256 = $serverHash
    smoke_exit_code = $smokeExit
    server_pid = $serverPid
    server_handle_exit = $directExit
    monitor_completion = $monitorComplete
    access_violation_events = $avCount
    positive_gxsm = $smokeText.Contains('hosted_target_exit_iteration_001=PASS target_sessions=2 exit_code=0')
    target_exits = ([regex]::Matches($traceText, 'TARGET_EXIT_NORMAL code=0')).Count
    debugger_teardowns = ([regex]::Matches($traceText, 'debugger_teardown=PASS')).Count
    source_shutdown_contract = $sourceShutdownContract
    lifecycle_shutdown_returned_successfully = ($smokeExit -eq 0 -and $directExit -match 'exit_code=0x0 .*read_result=PASS')
    remaining_server_processes = $remaining.Count
    failure_reasons = @($failures)
    evidence_root = $EvidenceRoot
}
$result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $EvidenceRoot 'result.json') -Encoding UTF8
$result | Format-List
if ($failures.Count -gt 0) { throw "Native app shutdown lifecycle regression failed: $($failures -join '; ')" }
Write-Output "NATIVE_APP_SHUTDOWN_LIFECYCLE_REGRESSION=PASS run_token=$RunToken"
