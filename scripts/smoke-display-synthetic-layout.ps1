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
        Name = 'display model viewport helper';
        Match = Find-FirstMatch (Join-Path $Root 'display_model.h') 'struct DisplayViewport';
    },
    [pscustomobject]@{
        Name = 'display model viewport index 1';
        Match = Find-FirstMatch (Join-Path $Root 'display_model.h') 'viewport.index = 1';
    },
    [pscustomobject]@{
        Name = 'display model viewport origin zero';
        Match = Find-FirstMatch (Join-Path $Root 'display_model.h') 'viewport.originX = 0';
    },
    [pscustomobject]@{
        Name = 'display model local to virtual helper';
        Match = Find-FirstMatch (Join-Path $Root 'display_model.h') 'virtualXFromLocal';
    },
    [pscustomobject]@{
        Name = 'display model viewport from monitor rect';
        Match = Find-FirstMatch (Join-Path $Root 'display_model.h') 'viewport.originX = selected->virtualX';
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
        Name = 'compositor viewport switch command';
        Match = Find-FirstMatch (Join-Path $Root 'server.cpp') 'desktop.display.viewport';
    },
    [pscustomobject]@{
        Name = 'compositor display viewport diagnostic';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'displayViewportDiagnostic()';
    },
    [pscustomobject]@{
        Name = 'compositor viewport switch state';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'g_hostedViewportIndex';
    },
    [pscustomobject]@{
        Name = 'compositor render viewport translation';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'SetViewportOrgEx';
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
    },
    [pscustomobject]@{
        Name = 'display options viewport indicator';
        Match = Find-FirstMatch (Join-Path $Root 'display_options.cpp') 'viewport.summary()';
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
