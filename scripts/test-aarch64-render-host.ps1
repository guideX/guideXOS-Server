[CmdletBinding()]
param(
    [string]$HostCxx = 'C:\mingw64\bin\g++.exe',
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot 'out\aarch64-render1-host'
} else {
    $OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
}
$outPrefix = $repoRoot.TrimEnd('\') + '\out\'
if (!$OutputDirectory.StartsWith($outPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing AARCH64 RENDER1 output outside repository out/: $OutputDirectory"
}
if (Test-Path -LiteralPath $OutputDirectory) {
    throw "Refusing to overwrite existing RENDER1 output: $OutputDirectory"
}
if (!(Test-Path -LiteralPath $HostCxx -PathType Leaf)) {
    $found = Get-Command g++ -ErrorAction SilentlyContinue
    if (!$found) { throw "Hosted C++ compiler not found: $HostCxx" }
    $HostCxx = $found.Source
}
$null = New-Item -ItemType Directory -Path $OutputDirectory -Force
$testBinary = Join-Path $OutputDirectory 'aarch64-render-host-tests.exe'
$sample = Join-Path $OutputDirectory 'developer-studio-render-demo.ppm'
$log = Join-Path $OutputDirectory 'render-host-results.txt'
$sources = @(
    (Join-Path $repoRoot 'tests\aarch64_render_host_tests.cpp'),
    (Join-Path $repoRoot 'kernel\core\aarch64_gui_render.cpp')
)
$arguments = @('-std=c++17','-Wall','-Wextra','-O2','-iquote',$repoRoot,
    '-I',(Join-Path $repoRoot 'sdk\include')) +
    $sources + @('-o',$testBinary)
Write-Host "Compiling production AARCH64 rendering callbacks with $HostCxx" -ForegroundColor Yellow
& $HostCxx @arguments
if ($LASTEXITCODE -ne 0) { throw 'Hosted AARCH64 rendering test compile failed' }
& $testBinary $sample 2>&1 | Tee-Object -FilePath $log
if ($LASTEXITCODE -ne 0) { throw 'Hosted AARCH64 rendering pixel tests failed' }
if (!(Test-Path -LiteralPath $sample -PathType Leaf)) { throw 'Hosted IDE-like rendering sample was not produced' }
Write-Host "AARCH64 RENDER1 hosted pixel tests: PASS. output=$OutputDirectory" -ForegroundColor Green
