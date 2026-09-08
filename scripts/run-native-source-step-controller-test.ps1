$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDirectory = Join-Path $repoRoot "out/validation"
$testBinary = Join-Path $outputDirectory "native-source-step-controller-test.exe"
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

$gxx = (Get-Command g++ -ErrorAction Stop).Source
& $gxx -std=c++14 -Wall -Wextra -Werror `
    "-idirafter$repoRoot" `
    (Join-Path $repoRoot "tests/native_source_step_controller_test.cpp") `
    -o $testBinary
if ($LASTEXITCODE -ne 0) { throw "source-step controller host test build failed" }
& $testBinary
if ($LASTEXITCODE -ne 0) { throw "source-step controller host test failed" }
