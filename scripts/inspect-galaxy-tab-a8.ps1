[CmdletBinding()]
param(
    [string]$Serial = '',
    [string]$FirmwareDirectory = '',
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot 'out\aarch64-tab-a8-t0'
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$outRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'out'))
if (!$OutputDirectory.StartsWith($outRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -and
    $OutputDirectory -ne $outRoot) {
    throw "Refusing generated reports outside repository out/: $OutputDirectory"
}
if (![string]::IsNullOrWhiteSpace($FirmwareDirectory)) {
    $FirmwareDirectory = [IO.Path]::GetFullPath($FirmwareDirectory)
    if (!(Test-Path -LiteralPath $FirmwareDirectory -PathType Container)) {
        throw "Firmware directory not found: $FirmwareDirectory"
    }
}

$pythonCommand = Get-Command python -ErrorAction SilentlyContinue
if ($null -eq $pythonCommand) {
    throw 'Python 3 is required for the host parser; no python executable was found on PATH.'
}
$parser = Join-Path $PSScriptRoot 'tab_a8_recon.py'
$arguments = @($parser, '--output-directory', $OutputDirectory, '--capture-device')
if (![string]::IsNullOrWhiteSpace($Serial)) { $arguments += @('--serial', $Serial) }
if (![string]::IsNullOrWhiteSpace($FirmwareDirectory)) { $arguments += @('--firmware-directory', $FirmwareDirectory) }

Write-Host 'Galaxy Tab A8 T0 inspection: ADB/procfs and supplied firmware only; no device writes.' -ForegroundColor Cyan
& $pythonCommand.Source @arguments
exit $LASTEXITCODE
