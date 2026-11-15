Set-StrictMode -Version Latest

function Get-C188R25HmpWords([string]$Text) {
    $words = [System.Collections.Generic.List[UInt64]]::new()
    foreach ($line in [regex]::Matches($Text,'(?im)^\s*(?:0x)?[0-9a-f]{1,16}:\s*(?<values>[^\r\n]+)')) {
        foreach ($word in [regex]::Matches($line.Groups['values'].Value,'(?i)0x([0-9a-f]{16})\b')) {
            $words.Add([Convert]::ToUInt64($word.Groups[1].Value,16))
        }
    }
    return $words.ToArray()
}

function Get-C188R25FatalCaptureAction(
    [bool]$MarkerSeen,
    [bool]$RecordValid,
    [bool]$PauseVerified) {
    if (-not $MarkerSeen) { return 'MONITOR_READINESS' }
    if ($RecordValid -and $PauseVerified) { return 'CAPTURED_FATAL' }
    return 'RESUME_AND_MONITOR'
}
