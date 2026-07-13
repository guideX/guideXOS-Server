param(
    [int]$TimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$probeScript = Join-Path $Root 'scripts\smoke-qemu-display-probe.ps1'

if (-not (Test-Path -LiteralPath $probeScript)) {
    throw "Missing probe smoke script: $probeScript"
}

& $probeScript -Backends @('virtio-gpu') -TimeoutSeconds $TimeoutSeconds -Mode compositorFrame
