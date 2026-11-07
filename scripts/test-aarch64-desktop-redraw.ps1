[CmdletBinding()]
param([string]$OutputDirectory = '')

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot 'out\aarch64-desktop-redraw-host-r1e'
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$outPrefix = $repoRoot.TrimEnd('\') + '\out\'
if (!$OutputDirectory.StartsWith($outPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing hosted desktop-redraw test output outside repository out/: $OutputDirectory"
}
if (Test-Path -LiteralPath $OutputDirectory) {
    throw "Refusing to overwrite existing hosted desktop-redraw test output: $OutputDirectory"
}
$null = New-Item -ItemType Directory -Path $OutputDirectory -Force

$compiler = 'C:\mingw64\bin\g++.exe'
if (!(Test-Path -LiteralPath $compiler -PathType Leaf)) {
    throw "Required LLVM compiler is missing: $compiler"
}
$executable = Join-Path $OutputDirectory 'aarch64_desktop_redraw_tests.exe'
$arguments = @(
    '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
    '-I', (Join-Path $repoRoot 'kernel\core\include'),
    (Join-Path $repoRoot 'tests\aarch64_desktop_redraw_tests.cpp'),
    '-o', $executable)
& $compiler @arguments
if ($LASTEXITCODE -ne 0) { throw 'Hosted desktop-redraw regression compile failed.' }
$null = & $executable 2>&1
if ($LASTEXITCODE -ne 0) { throw 'Hosted desktop-redraw regression failed.' }
$text = 'aarch64DesktopRedrawChecks=27'
$text | Set-Content -LiteralPath (Join-Path $OutputDirectory 'result.log') -Encoding utf8
Write-Output $text
Write-Host '[desktop-redraw] hosted invalidation checks: PASS' -ForegroundColor Green
