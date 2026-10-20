function Read-C181QmpResponse {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][System.IO.StreamReader]$Reader,
        [Parameter(Mandatory=$true)][string]$LogPath,
        [Parameter(Mandatory=$true)][string]$Command,
        [int]$ResetEventWaitMilliseconds=5000
    )

    $response = $null
    $resetObserved = $false
    while ($true) {
        $line = $Reader.ReadLine()
        if ($null -eq $line) { throw 'QMP closed its connection.' }
        if ($line -match '"event"\s*:') {
            Add-Content -LiteralPath $LogPath -Encoding ascii -Value ("qmp-event=" + $line)
            if ($line -match '"event"\s*:\s*"RESET"') { $resetObserved = $true }
            continue
        }
        if ($line -match '"return"|"error"') {
            $response = $line
            break
        }
        Add-Content -LiteralPath $LogPath -Encoding ascii -Value ("qmp-unclassified=" + $line)
    }

    if ($Command -eq 'system_reset' -and -not $resetObserved) {
        $deadline = [DateTime]::UtcNow.AddMilliseconds([Math]::Max(0,$ResetEventWaitMilliseconds))
        $pendingRead = $Reader.ReadLineAsync()
        while ([DateTime]::UtcNow -lt $deadline) {
            try {
                if (-not $pendingRead.Wait(100)) { continue }
                $line = $pendingRead.GetAwaiter().GetResult()
            } catch {
                Add-Content -LiteralPath $LogPath -Encoding ascii -Value ("reset-event-observation=STREAM_ERROR " + $_.Exception.GetType().Name)
                break
            }
            if ($null -eq $line) { break }
            if ($line -match '"event"\s*:') {
                Add-Content -LiteralPath $LogPath -Encoding ascii -Value ("qmp-event=" + $line)
                if ($line -match '"event"\s*:\s*"RESET"') { $resetObserved = $true; break }
            } else {
                Add-Content -LiteralPath $LogPath -Encoding ascii -Value ("qmp-unclassified=" + $line)
            }
            $pendingRead = $Reader.ReadLineAsync()
        }
        $observation = if ($resetObserved) { 'RESET_SEEN' } else { 'RESET_NOT_OBSERVED' }
        Add-Content -LiteralPath $LogPath -Encoding ascii -Value ("reset-event-observation=" + $observation)
    }

    return $response
}

Export-ModuleMember -Function Read-C181QmpResponse
