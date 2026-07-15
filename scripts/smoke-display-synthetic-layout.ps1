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
        Name = 'display model dual-window gate';
        Match = Find-FirstMatch (Join-Path $Root 'display_model.h') 'GXOS_SYNTHETIC_DUAL_WINDOW_OUTPUT';
    },
    [pscustomobject]@{
        Name = 'display model dual-window helper';
        Match = Find-FirstMatch (Join-Path $Root 'display_model.h') 'hostedSyntheticDualWindowOutputEnabled';
    },
    [pscustomobject]@{
        Name = 'display model dual desktop helper';
        Match = Find-FirstMatch (Join-Path $Root 'display_model.h') 'makeSyntheticDualMonitorDesktop';
    },
    [pscustomobject]@{
        Name = 'display model diagnostic framebuffer helper';
        Match = Find-FirstMatch (Join-Path $Root 'display_model.h') 'makeDiagnosticFramebufferMonitor';
    },
    [pscustomobject]@{
        Name = 'display model viewport helper';
        Match = Find-FirstMatch (Join-Path $Root 'display_model.h') 'struct DisplayViewport';
    },
    [pscustomobject]@{
        Name = 'display model monitor lookup by point';
        Match = Find-FirstMatch (Join-Path $Root 'display_model.h') 'findMonitorByVirtualPoint';
    },
    [pscustomobject]@{
        Name = 'display model monitor lookup by rect';
        Match = Find-FirstMatch (Join-Path $Root 'display_model.h') 'findMonitorByVirtualRectLargestIntersection';
    },
    [pscustomobject]@{
        Name = 'display model monitor bounds helper';
        Match = Find-FirstMatch (Join-Path $Root 'display_model.h') 'monitorBounds(';
    },
    [pscustomobject]@{
        Name = 'display model monitor work area helper';
        Match = Find-FirstMatch (Join-Path $Root 'display_model.h') 'monitorWorkArea(';
    },
    [pscustomobject]@{
        Name = 'display model primary monitor work area helper';
        Match = Find-FirstMatch (Join-Path $Root 'display_model.h') 'primaryMonitorWorkArea(';
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
        Name = 'display model render target struct';
        Match = Find-FirstMatch (Join-Path $Root 'display_model.h') 'struct DisplayRenderTarget';
    },
    [pscustomobject]@{
        Name = 'display model render target builder';
        Match = Find-FirstMatch (Join-Path $Root 'display_model.h') 'buildDisplayRenderTargets(';
    },
    [pscustomobject]@{
        Name = 'display model render target lookup';
        Match = Find-FirstMatch (Join-Path $Root 'display_model.h') 'displayRenderTargetForIndex(';
    },
    [pscustomobject]@{
        Name = 'display model hosted fallback render target';
        Match = Find-FirstMatch (Join-Path $Root 'display_model.h') 'makeHostedFallbackRenderTarget(';
    },
    [pscustomobject]@{
        Name = 'display model single target fallback path';
        Match = Find-FirstMatch (Join-Path $Root 'display_model.h') 'targets.push_back(makeHostedFallbackRenderTarget(fallbackWidth, fallbackHeight, nullptr, false))';
    },
    [pscustomobject]@{
        Name = 'display model dual-window backing gate';
        Match = Find-FirstMatch (Join-Path $Root 'display_model.h') 'backedByHostedFramebuffer = dualWindowOutput ? true : isActive';
    },
    [pscustomobject]@{
        Name = 'display model active render target helper';
        Match = Find-FirstMatch (Join-Path $Root 'display_model.h') 'activeDisplayRenderTarget(';
    },
    [pscustomobject]@{
        Name = 'display model render target summary';
        Match = Find-FirstMatch (Join-Path $Root 'display_model.h') 'displayRenderTargetsSummary(';
    },
    [pscustomobject]@{
        Name = 'display model viewport local rect conversion';
        Match = Find-FirstMatch (Join-Path $Root 'display_model.h') 'localRectFromVirtual';
    },
    [pscustomobject]@{
        Name = 'display model detailed summary';
        Match = Find-FirstMatch (Join-Path $Root 'display_model.h') 'detailedSummary() const';
    },
    [pscustomobject]@{
        Name = 'compositor synthetic extend mode helper';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'syntheticExtendModeActive';
    },
    [pscustomobject]@{
        Name = 'compositor primary taskbar rect helper';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'primaryTaskbarDisplayRect';
    },
    [pscustomobject]@{
        Name = 'compositor taskbar visibility helper';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'hostedPrimaryTaskbarVisibleInViewport';
    },
    [pscustomobject]@{
        Name = 'compositor monitor work area for window';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'monitorWorkAreaForWindow';
    },
    [pscustomobject]@{
        Name = 'compositor monitor work area for point';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'monitorWorkAreaForPoint';
    },
    [pscustomobject]@{
        Name = 'compositor synthetic layout log';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'dualWindowOutput=';
    },
    [pscustomobject]@{
        Name = 'compositor dual-window log';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'dual-window output enabled via GXOS_SYNTHETIC_DUAL_WINDOW_OUTPUT=1';
    },
    [pscustomobject]@{
        Name = 'compositor hosted render target helper';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'hostedRenderTargetForDesktop';
    },
    [pscustomobject]@{
        Name = 'compositor hosted render targets helper';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'hostedRenderTargetsForDesktop';
    },
    [pscustomobject]@{
        Name = 'compositor hosted render target for window helper';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'hostedRenderTargetForWindow';
    },
    [pscustomobject]@{
        Name = 'compositor hosted viewport for window helper';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'hostedViewportForWindow';
    },
    [pscustomobject]@{
        Name = 'compositor bare-metal render target helper';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'bareMetalRenderTargetForFramebuffer';
    },
    [pscustomobject]@{
        Name = 'compositor bare-metal render target bridge';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'target.backedByHostedFramebuffer = true;';
    },
    [pscustomobject]@{
        Name = 'compositor bare-metal render target viewport';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'const int fbW = std::max(1, viewport.width);';
    },
    [pscustomobject]@{
        Name = 'compositor bare-metal render target overload';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'renderToFramebuffer(const DisplayRenderTarget& renderTarget)';
    },
    [pscustomobject]@{
        Name = 'compositor bare-metal render target call site';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'renderToFramebuffer(renderTarget);';
    },
    [pscustomobject]@{
        Name = 'compositor bare-metal render target diagnostics';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'renderTarget.summary()';
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
        Name = 'compositor display summary render targets';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'displayRenderTargetsSummary(renderTargets)';
    },
    [pscustomobject]@{
        Name = 'compositor display summary dual-window output';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'presentationMode=';
    },
    [pscustomobject]@{
        Name = 'compositor viewport diagnostic active target';
        Match = Find-FirstMatch (Join-Path $Root 'display_model.h') 'activeHostedTarget=';
    },
    [pscustomobject]@{
        Name = 'compositor viewport diagnostic render target count';
        Match = Find-FirstMatch (Join-Path $Root 'display_model.h') 'renderTargetCount=';
    },
    [pscustomobject]@{
        Name = 'compositor viewport diagnostic backed target count';
        Match = Find-FirstMatch (Join-Path $Root 'display_model.h') 'backedTargetCount=';
    },
    [pscustomobject]@{
        Name = 'compositor viewport diagnostic active viewport origin';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'activeViewportOrigin=';
    },
    [pscustomobject]@{
        Name = 'compositor viewport diagnostic primary monitor';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'primaryMonitorId=';
    },
    [pscustomobject]@{
        Name = 'compositor viewport diagnostic active work area';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'activeWork=';
    },
    [pscustomobject]@{
        Name = 'compositor viewport diagnostic primary work area';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'primaryWork=';
    },
    [pscustomobject]@{
        Name = 'compositor viewport diagnostic taskbar ownership';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'taskbarPrimaryOnly=';
    },
    [pscustomobject]@{
        Name = 'compositor viewport diagnostic taskbar visible';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'taskbarVisible=';
    },
    [pscustomobject]@{
        Name = 'compositor viewport switch state';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'g_hostedViewportIndex';
    },
    [pscustomobject]@{
        Name = 'compositor viewport switch safe dual-window';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'dual-window output is active and both synthetic monitors are already visible';
    },
    [pscustomobject]@{
        Name = 'compositor render target viewport translation';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'renderTarget.viewportDescriptor()';
    },
    [pscustomobject]@{
        Name = 'compositor display summary command';
        Match = Find-FirstMatch (Join-Path $Root 'server.cpp') 'desktop.display.summary';
    },
    [pscustomobject]@{
        Name = 'compositor summary mode label';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'normal-single-output';
    },
    [pscustomobject]@{
        Name = 'compositor summary synthetic camera label';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'synthetic-camera';
    },
    [pscustomobject]@{
        Name = 'compositor summary synthetic two-window label';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'synthetic-two-window-output';
    },
    [pscustomobject]@{
        Name = 'compositor state load restore geometry helper';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'applyLoadedWindowGeometry';
    },
    [pscustomobject]@{
        Name = 'compositor state load restore clamp helper';
        Match = Find-FirstMatch (Join-Path $Root 'compositor.cpp') 'sanitizeWindowRestoreRect';
    },
    [pscustomobject]@{
        Name = 'desktop state restore fields';
        Match = Find-FirstMatch (Join-Path $Root 'desktop_state.h') 'restoreX';
    },
    [pscustomobject]@{
        Name = 'desktop config restore fields';
        Match = Find-FirstMatch (Join-Path $Root 'desktop_config.h') 'restoreX';
    },
    [pscustomobject]@{
        Name = 'desktop config restore serialization';
        Match = Find-FirstMatch (Join-Path $Root 'desktop_config.h') '"restoreH"';
    },
    [pscustomobject]@{
        Name = 'display options synthetic note';
        Match = Find-FirstMatch (Join-Path $Root 'display_options.cpp') 'Synthetic dual-monitor test mode is active';
    },
    [pscustomobject]@{
        Name = 'display options viewport primary taskbar note';
        Match = Find-FirstMatch (Join-Path $Root 'display_options.cpp') 'wallpaper/taskbar follow the selected primary monitor';
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
