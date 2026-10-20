$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot 'c181-qmp.psm1') -Force -ErrorAction Stop
$tempBase = Join-Path ([System.IO.Path]::GetTempPath()) ('c181-qmp-' + [Guid]::NewGuid().ToString('N'))
$seenPath = $tempBase + '-seen.log'
$missingPath = $tempBase + '-missing.log'
try {
    $afterResponse = '{"return": {}}' + "`n" + '{"timestamp":{"seconds":1,"microseconds":2},"event":"RESET","data":{"guest":false}}' + "`n"
    $memory = [System.IO.MemoryStream]::new([System.Text.Encoding]::ASCII.GetBytes($afterResponse))
    $reader = [System.IO.StreamReader]::new($memory,[System.Text.Encoding]::ASCII,$false,1024,$true)
    try {
        $response = Read-C181QmpResponse -Reader $reader -LogPath $seenPath -Command 'system_reset' -ResetEventWaitMilliseconds 500
    } finally { $reader.Dispose(); $memory.Dispose() }
    $seenLog = Get-Content -LiteralPath $seenPath -Raw
    if ($response -ne '{"return": {}}') { throw "Unexpected QMP response: $response" }
    if ($seenLog -notmatch 'qmp-event=.*"event":"RESET"') { throw 'RESET event was not written to the QMP evidence log.' }
    if ($seenLog -notmatch 'reset-event-observation=RESET_SEEN') { throw 'The reset observation did not record the RESET event.' }

    $withoutReset = '{"return": {}}' + "`n"
    $memory = [System.IO.MemoryStream]::new([System.Text.Encoding]::ASCII.GetBytes($withoutReset))
    $reader = [System.IO.StreamReader]::new($memory,[System.Text.Encoding]::ASCII,$false,1024,$true)
    try {
        [void](Read-C181QmpResponse -Reader $reader -LogPath $missingPath -Command 'system_reset' -ResetEventWaitMilliseconds 50)
    } finally { $reader.Dispose(); $memory.Dispose() }
    $missingLog = Get-Content -LiteralPath $missingPath -Raw
    if ($missingLog -notmatch 'reset-event-observation=RESET_NOT_OBSERVED') { throw 'Missing RESET event was not reported explicitly.' }
    Write-Output 'C181 QMP event capture: PASS (RESET events are logged; missing events remain explicit).'
} finally {
    Remove-Item -LiteralPath $seenPath,$missingPath -Force -ErrorAction SilentlyContinue
}
