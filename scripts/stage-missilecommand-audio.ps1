# MC5: stage Missile Command audio resources deterministically.
#
# Source (read-only, never modified): D:\dev\bkup\inactive\missilecommand
#   Alarm.WAV / Empty.WAV / EXPLODE.WAV / Split.WAV / Ohno.wav are PCM and are
#   copied byte-identical. Swoosh.wav is MS-ADPCM stereo and is converted to
#   native PCM16 mono 22050 Hz by scripts/convert-missilecommand-swoosh.py
#   (same policy as the build1.gif -> city.gximg art conversion: preserve the
#   original, document the transform, verify the output, keep it
#   deterministic). Thunder.wav / Error.wav are missing even in the original
#   project and stay silent; OnNo.wav is commented out upstream and unused.
#
# Output: sdk/samples/missilecommand/resources/audio/*.wav, staged to
# Apps/MissileCommand by sdk/build-samples.ps1 like DD.ini / city.gximg.

[CmdletBinding()]
param(
    [string]$SourceDir = 'D:\dev\bkup\inactive\missilecommand'
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root

$AudioDir = Join-Path $Root 'sdk\samples\missilecommand\resources\audio'
New-Item -ItemType Directory -Force -Path $AudioDir | Out-Null

$Copies = @(
    @('Alarm.WAV', 'alarm.wav'),
    @('Empty.WAV', 'empty.wav'),
    @('EXPLODE.WAV', 'explode.wav'),
    @('Split.WAV', 'split.wav'),
    @('Ohno.wav', 'ohno.wav')
)

foreach ($pair in $Copies) {
    $source = Join-Path $SourceDir $pair[0]
    $destination = Join-Path $AudioDir $pair[1]
    if (-not (Test-Path -LiteralPath $source)) { throw "Original asset missing: $source" }
    Copy-Item -LiteralPath $source -Destination $destination -Force
    Write-Host ("staged {0} -> {1} ({2} bytes)" -f $pair[0], $pair[1], (Get-Item -LiteralPath $destination).Length)
}

$python = Get-Command python.exe -ErrorAction SilentlyContinue
if (-not $python) { throw 'python.exe was not found (needed for the deterministic Swoosh ADPCM conversion).' }
$swooshSource = Join-Path $SourceDir 'Swoosh.wav'
$swooshDest = Join-Path $AudioDir 'swoosh.wav'
& $python.Source (Join-Path $Root 'scripts\convert-missilecommand-swoosh.py') $swooshSource $swooshDest
if ($LASTEXITCODE -ne 0) { throw "Swoosh conversion failed with exit code $LASTEXITCODE." }

Write-Host 'Missile Command audio resources staged.'
