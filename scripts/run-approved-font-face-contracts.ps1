#Requires -Version 5.1
[CmdletBinding()]
param(
	[string]$OutputDirectory = "out\font13-validation\font13r-face-contracts"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root

$Output = if ([IO.Path]::IsPathRooted($OutputDirectory)) {
	$OutputDirectory
} else {
	Join-Path $Root $OutputDirectory
}
New-Item -ItemType Directory -Force -Path $Output | Out-Null

$PreviousFixture = $env:GXOS_FONT_TEST_FIXTURE_PATH
$PreviousStyle = $env:GXOS_FONT_TEST_EXPECTED_STYLE
$faces = @(
	@{ File = "Roboto-v2.138-Regular.ttf"; Style = "Regular" },
	@{ File = "Roboto-v2.138-Bold.ttf"; Style = "Bold" },
	@{ File = "Roboto-v2.138-Italic.ttf"; Style = "Italic" }
)

try {
	foreach ($face in $faces) {
		$fixture = Join-Path $Root (Join-Path "assets\Fonts\Roboto-v2.138-package" $face.File)
		if (-not (Test-Path -LiteralPath $fixture -PathType Leaf)) {
			throw "Approved face is missing: $fixture"
		}
		$env:GXOS_FONT_TEST_FIXTURE_PATH = $fixture
		$env:GXOS_FONT_TEST_EXPECTED_STYLE = $face.Style
		$safeStyle = $face.Style.ToLowerInvariant()
		& (Join-Path $Root "scripts\run-font-truetype-test.ps1") -OutputPath (Join-Path $Output "font3-$safeStyle.exe")
		& (Join-Path $Root "scripts\run-font-raster-test.ps1") -OutputPath (Join-Path $Output "font4-$safeStyle.exe")
	}
} finally {
	if ($null -eq $PreviousFixture) { Remove-Item Env:GXOS_FONT_TEST_FIXTURE_PATH -ErrorAction SilentlyContinue }
	else { $env:GXOS_FONT_TEST_FIXTURE_PATH = $PreviousFixture }
	if ($null -eq $PreviousStyle) { Remove-Item Env:GXOS_FONT_TEST_EXPECTED_STYLE -ErrorAction SilentlyContinue }
	else { $env:GXOS_FONT_TEST_EXPECTED_STYLE = $PreviousStyle }
}

Write-Host "FONT3 parser and FONT4 raster contracts passed for all approved package faces."
