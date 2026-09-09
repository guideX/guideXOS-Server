$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDirectory = Join-Path $repoRoot "out/validation"
$testBinary = Join-Path $outputDirectory "native-call-stack-controller-test.exe"
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

$gxx = (Get-Command g++ -ErrorAction Stop).Source
& $gxx -std=c++14 -Wall -Wextra -Werror `
    "-idirafter$repoRoot" `
    (Join-Path $repoRoot "tests/native_call_stack_controller_test.cpp") `
    -o $testBinary
if ($LASTEXITCODE -ne 0) { throw "call-stack controller host test build failed" }
& $testBinary
if ($LASTEXITCODE -ne 0) { throw "call-stack controller host test failed" }
