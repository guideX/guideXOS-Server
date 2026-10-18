[CmdletBinding()]
param(
    [string]$ArtifactRoot = ""
)

$ErrorActionPreference = "Stop"
$modulePath = Join-Path (Split-Path -Parent $PSScriptRoot) "scripts\pacman-source-registration.psm1"
if ([string]::IsNullOrWhiteSpace($ArtifactRoot)) {
    $ArtifactRoot = Join-Path ([System.IO.Path]::GetTempPath()) "pacman-source-registration-tests"
}
$runRoot = Join-Path $ArtifactRoot ("registration-" + (Get-Date -Format "yyyyMMdd-HHmmss-fff"))
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
Import-Module -Name $modulePath -Force

function New-Fixture([string]$Name, [bool]$AudioReferences, [bool]$AudioDefinitions) {
    $fixtureRoot = Join-Path $runRoot $Name
    $src = Join-Path $fixtureRoot "src"
    New-Item -ItemType Directory -Path $src -Force | Out-Null
    foreach ($fileName in @("main.cpp", "game.cpp", "level.cpp", "level_rules.cpp", "renderer.cpp", "bitmap_loader.cpp")) {
        [System.IO.File]::WriteAllText((Join-Path $src $fileName), "// fixture`n")
    }
    if ($AudioReferences) {
        [System.IO.File]::WriteAllText((Join-Path $src "main.cpp"), "void use_audio(gx_app_context* ctx) { pacman_audio_load_resources(ctx); pacman_audio_submit(ctx, sound); }`n")
    }
    if ($AudioDefinitions) {
        [System.IO.File]::WriteAllText((Join-Path $src "pacman_audio.cpp"), @"
uint32_t pacman_audio_load_resources(gx_app_context* ctx) {
    return 0;
}
PacManAudioSubmitResult pacman_audio_submit(void* userData, PacManSoundId sound) {
    return kPacManAudioSilent;
}
"@)
    }
    return $fixtureRoot
}

$preAudioRoot = New-Fixture "pre-audio" $false $false
$preAudio = Get-PacManSourceRegistration -SourceRoot $preAudioRoot
if ($preAudio.AudioRequired -or @($preAudio.Sources).Count -ne 6 -or
    @($preAudio.Sources | Where-Object Name -eq "pacman_audio").Count -ne 0) {
    throw "Pre-audio source registration incorrectly requires an audio object."
}

$audioRoot = New-Fixture "audio-bearing" $true $true
$audio = Get-PacManSourceRegistration -SourceRoot $audioRoot
if (-not $audio.AudioRequired -or @($audio.Sources).Count -ne 7 -or
    @($audio.Sources | Where-Object Name -eq "pacman_audio").Count -ne 1) {
    throw "Audio-bearing source registration did not add exactly one audio object."
}

$missingImplementationRoot = New-Fixture "missing-implementation" $true $false
$missingImplementationRejected = $false
try {
    Get-PacManSourceRegistration -SourceRoot $missingImplementationRoot | Out-Null
} catch {
    $missingImplementationRejected = $_.Exception.Message -match "no implementation"
}
if (-not $missingImplementationRejected) {
    throw "Audio-bearing source without pacman_audio.cpp was not rejected."
}

$missingDefinitionRoot = New-Fixture "missing-definition" $true $true
[System.IO.File]::WriteAllText((Join-Path $missingDefinitionRoot "src\pacman_audio.cpp"), "// no required definitions`n")
$missingDefinitionRejected = $false
try {
    Get-PacManSourceRegistration -SourceRoot $missingDefinitionRoot | Out-Null
} catch {
    $missingDefinitionRejected = $_.Exception.Message -match "does not define"
}
if (-not $missingDefinitionRejected) {
    throw "Audio-bearing source with missing audio definitions was not rejected."
}

Write-Output "PacMan source registration tests passed: pre-audio, audio-bearing, missing implementation, missing definitions."
Write-Output "Fixture evidence: $runRoot"
