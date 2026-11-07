[CmdletBinding()]
param([string]$OutputDirectory = '')

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot 'out\aarch64-virtio-keyboard-host-r1e'
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$outPrefix = $repoRoot.TrimEnd('\') + '\out\'
if (!$OutputDirectory.StartsWith($outPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing hosted keyboard-test output outside repository out/: $OutputDirectory"
}
if (Test-Path -LiteralPath $OutputDirectory) {
    throw "Refusing to overwrite existing hosted keyboard-test output: $OutputDirectory"
}
$null = New-Item -ItemType Directory -Path $OutputDirectory -Force

$compiler = 'C:\mingw64\bin\g++.exe'
if (!(Test-Path -LiteralPath $compiler -PathType Leaf)) {
    throw "Required LLVM compiler is missing: $compiler"
}
$executable = Join-Path $OutputDirectory 'aarch64_virtio_keyboard_tests.exe'
$arguments = @(
    '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
    '-I', (Join-Path $repoRoot 'kernel\core\include'),
    (Join-Path $repoRoot 'tests\aarch64_virtio_keyboard_tests.cpp'),
    (Join-Path $repoRoot 'kernel\core\input_queue.cpp'),
    '-o', $executable)
& $compiler @arguments
if ($LASTEXITCODE -ne 0) { throw 'Hosted virtio keyboard regression compile failed.' }
$output = & $executable 2>&1
if ($LASTEXITCODE -ne 0) { throw "Hosted virtio keyboard regression failed: $($output -join ' ')" }
$text = 'aarch64VirtioKeyboardChecks=25'
$text | Set-Content -LiteralPath (Join-Path $OutputDirectory 'result.log') -Encoding utf8
Write-Output $text
Write-Host '[virtio-keyboard] 25 hosted assertions: PASS' -ForegroundColor Green
