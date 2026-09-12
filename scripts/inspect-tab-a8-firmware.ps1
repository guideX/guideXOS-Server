[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$FirmwareDirectory,
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$FirmwareDirectory = [IO.Path]::GetFullPath($FirmwareDirectory)
if (!(Test-Path -LiteralPath $FirmwareDirectory -PathType Container)) {
    throw "Firmware directory not found: $FirmwareDirectory"
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot 'out\aarch64-tab-a8-t0'
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$outRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'out'))
if (!$OutputDirectory.StartsWith($outRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -and
    $OutputDirectory -ne $outRoot) {
    throw "Refusing generated reports outside repository out/: $OutputDirectory"
}

$pythonCommand = Get-Command python -ErrorAction SilentlyContinue
if ($null -eq $pythonCommand) {
    throw 'Python 3 is required for the host parser; no python executable was found on PATH.'
}
$parser = Join-Path $PSScriptRoot 'tab_a8_recon.py'
Write-Host 'Galaxy Tab A8 firmware inspection: source files are read-only and never uploaded.' -ForegroundColor Cyan
& $pythonCommand.Source $parser --output-directory $OutputDirectory --firmware-directory $FirmwareDirectory
exit $LASTEXITCODE
