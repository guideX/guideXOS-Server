Set-StrictMode -Version Latest

function Get-C188R25FatalCaptureAction(
    [bool]$MarkerSeen,
    [bool]$RecordValid,
    [bool]$PauseVerified) {
    if (-not $MarkerSeen) { return 'MONITOR_READINESS' }
    if ($RecordValid -and $PauseVerified) { return 'CAPTURED_FATAL' }
    return 'RESUME_AND_MONITOR'
}
