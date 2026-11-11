[CmdletBinding()]
param(
    [string]$OutputDirectory = 'E:\CodexArtifacts\S22R21\tests'
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$compiler = Get-Command g++ -ErrorAction Stop
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$binary = Join-Path $OutputDirectory 'display-options-preview-policy-test.exe'
$source = Join-Path $repoRoot 'tests\display_options_preview_policy_test.cpp'
$compileArgs = @(
    '-std=c++14', '-Wall', '-Wextra', '-Werror',
    '-iquote', (Join-Path $repoRoot 'kernel\core\include'),
    $source, '-o', $binary
)

& $compiler.Source @compileArgs
if ($LASTEXITCODE -ne 0) { throw "Display Options preview policy test compilation failed ($LASTEXITCODE)." }
& $binary
if ($LASTEXITCODE -ne 0) { throw "Display Options preview policy tests failed ($LASTEXITCODE)." }

$kernelApps = Get-Content -LiteralPath (Join-Path $repoRoot 'kernel\core\kernel_apps.cpp') -Raw

function Require-Text([string]$Text, [string]$Message) {
    if (-not $Text.Contains($Message)) { throw "Display Options UI contract failed: $Message" }
}

function Get-Section([string]$Source, [string]$Start, [string]$End) {
    $startIndex = $Source.IndexOf($Start, [StringComparison]::Ordinal)
    if ($startIndex -lt 0) { throw "Display Options UI contract section is missing: $Start" }
    $endIndex = $Source.IndexOf($End, $startIndex + $Start.Length, [StringComparison]::Ordinal)
    if ($endIndex -lt 0) { throw "Display Options UI contract section end is missing: $End" }
    return $Source.Substring($startIndex, $endIndex - $startIndex)
}

$query = Get-Section $kernelApps 'bool DisplayOptionsApp::queryDisplayConfiguration()' 'bool DisplayOptionsApp::queryPendingTopologyChange()'
$draw = Get-Section $kernelApps 'void DisplayOptionsApp::drawDisplayTab(' 'bool DisplayOptionsApp::queryDisplayConfiguration()'
$submit = Get-Section $kernelApps 'bool DisplayOptionsApp::submitDisplayConfiguration(' 'void DisplayOptionsApp::cancelDisplayConfiguration()'
$cancel = Get-Section $kernelApps 'void DisplayOptionsApp::cancelDisplayConfiguration()' 'void DisplayOptionsApp::draw('
$mouse = Get-Section $kernelApps 'void DisplayOptionsApp::onMouseDown(' 'void DisplayOptionsApp::onMouseMove('
$keys = Get-Section $kernelApps 'void DisplayOptionsApp::onKeyDown(' 'void DisplayOptionsApp::onKeyChar('
$load = Get-Section $kernelApps 'void DisplayOptionsApp::loadSelection()' 'bool DisplayOptionsApp::init()'
$close = Get-Section $kernelApps 'void DisplayOptionsApp::onWindowClose()' 'void DisplayOptionsApp::setActiveTab('

Require-Text $query 'shouldQueryConfigurationBackend(public_preview_display_configuration())'
Require-Text $query 'configurationUnavailableMessage(m_displayConfigurationAvailability)'
Require-Text $draw 'configurationUnavailableMessage(m_displayConfigurationAvailability)'
Require-Text $draw '"Close"'
Require-Text $draw '"Extend"'
if ($draw.IndexOf('return;', [StringComparison]::Ordinal) -gt $draw.IndexOf('"Extend"', [StringComparison]::Ordinal)) {
    throw 'Display Options UI contract failed: configuration controls remain reachable after the unavailable-state branch.'
}
Require-Text $submit 'isConfigurationActionEnabled('
Require-Text $cancel 'isConfigurationActionEnabled('
Require-Text $mouse 'ConfigurationAction::Close'
Require-Text $mouse 'submitDisplayConfiguration(false)'
if ($mouse.IndexOf('ConfigurationAction::Apply', [StringComparison]::Ordinal) -gt
    $mouse.IndexOf('submitDisplayConfiguration(false)', [StringComparison]::Ordinal)) {
    throw 'Display Options UI contract failed: the unavailable mouse guard occurs after the Apply path.'
}
Require-Text $keys 'ConfigurationAction::Apply'
Require-Text $keys 'submitDisplayConfiguration(false)'
Require-Text $load 'queryDisplayConfiguration();'
Require-Text $close '++m_windowGeneration;'
Require-Text $kernelApps 'SettingsNativeApp::create'
Require-Text $kernelApps 'drawDisplayInformation(contentX, contentW, cardY);'

Write-Host 'Display Options preview UI source and lifecycle checks PASS.'
