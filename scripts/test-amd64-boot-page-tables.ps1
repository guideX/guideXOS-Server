param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
$compiler = Get-Command g++ -ErrorAction Stop
$testExecutable = Join-Path $outputPath 'amd64_boot_page_tables_test.exe'
$sources = @('tests/amd64_boot_page_tables_test.cpp', 'guideXOSBootLoader/paging.cpp') |
    ForEach-Object { Join-Path $repoRoot $_ }

& $compiler.Source -std=c++14 -O2 -Wall -Wextra -Werror @sources -o $testExecutable
if ($LASTEXITCODE -ne 0) { throw "AMD64 boot paging test compilation failed with exit code $LASTEXITCODE" }

& $testExecutable
if ($LASTEXITCODE -ne 0) { throw "AMD64 boot paging tests failed with exit code $LASTEXITCODE" }
