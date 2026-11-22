param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputPath = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null

$compiler = Get-Command g++ -ErrorAction Stop
$target = & $compiler.Source -dumpmachine
if ($LASTEXITCODE -ne 0 -or $target -notmatch '^x86_64-') {
    throw "The hosted AMD64 CPUID test requires an x86_64 g++ target, found '$target'."
}
$testSource = Join-Path $repoRoot 'tests\amd64_cpu_capabilities_test.cpp'
$cpuSource = Join-Path $repoRoot 'kernel\arch\amd64\cpu_capabilities.cpp'
$testExecutable = Join-Path $outputPath 'amd64_cpu_capabilities_test.exe'

& $compiler.Source -std=c++14 -O2 -Wall -Wextra -Werror $testSource $cpuSource -o $testExecutable
if ($LASTEXITCODE -ne 0) { throw "AMD64 CPUID test compilation failed with exit code $LASTEXITCODE" }

& $testExecutable
if ($LASTEXITCODE -ne 0) { throw "AMD64 CPUID tests failed with exit code $LASTEXITCODE" }
