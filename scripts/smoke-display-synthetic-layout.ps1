param(
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

function Find-FirstMatch {
    param(
        [string]$Path,
        [string]$Pattern
    )
    if (-not (Test-Path $Path)) {
        return $null
    }
    return Select-String -Path $Path -Pattern $Pattern -SimpleMatch | Select-Object -First 1
}

$checks = @(
    [pscustomobject]@{
        Name = 'display model synthetic gate';
        Match = Find-FirstMatch (Join-Path $Root 'display_model.h') 'GXOS_SYNTHETIC_DUAL_MONITOR';
    },
    [pscustomobject]@{
        Name = 'display model dual desktop helper';
        Match = Find-FirstMatch (Join-Path $Root 'display_model.h') 'makeSyntheticDualMonitorDesktop';
    },
    [pscustomobject]@{
        Name = 'display model detailed summary';
        Match = Find-FirstMatch (Join-Path $Root 'display_model.h') 'detailedSummary() const';
    },
    [pscustomobject]@{
        Name = 'compositor synthetic layout log';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'synthetic dual-monitor mode enabled via GXOS_SYNTHETIC_DUAL_MONITOR=1';
    },
    [pscustomobject]@{
        Name = 'compositor display summary command';
        Match = Find-FirstMatch (Join-Path $Root 'server.cpp') 'desktop.display.summary';
    },
    [pscustomobject]@{
        Name = 'display options synthetic note';
        Match = Find-FirstMatch (Join-Path $Root 'display_options.cpp') 'Synthetic dual-monitor test mode is active';
    },
    [pscustomobject]@{
        Name = 'display options synthetic preview gate';
        Match = Find-FirstMatch (Join-Path $Root 'display_options.cpp') 'hostedSyntheticDualMonitorEnabled()';
    }
)

$failed = @()
foreach ($check in $checks) {
    $pass = $null -ne $check.Match
    if (-not $pass) {
        $failed += $check.Name
    }
    if (-not $Quiet) {
        $status = if ($pass) { 'PASS' } else { 'FAIL' }
        Write-Host ("[{0}] {1}" -f $status, $check.Name)
        if ($check.Match) {
            Write-Host ("       {0}:{1}" -f $check.Match.Path, $check.Match.LineNumber)
        }
    }
}

if ($failed.Count -gt 0) {
    throw ('Synthetic display smoke failed: ' + ($failed -join ', '))
}

if (-not $Quiet) {
    Write-Host 'Synthetic display smoke passed.'
}
