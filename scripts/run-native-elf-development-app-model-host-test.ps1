$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$gxx = (Get-Command g++ -ErrorAction Stop).Source
$outputDirectory = Join-Path $repoRoot "tmp/native-elf-development-app-model-host-test"
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$output = Join-Path $outputDirectory "native_elf_development_app_model_host_test.exe"
& $gxx -std=c++17 -O2 -Wall -Wextra -Werror `
    (Join-Path $repoRoot "tests/native_elf_development_app_model_host_test.cpp") `
    (Join-Path $repoRoot "kernel/core/native_elf/native_elf_development_app_model.cpp") `
    -o $output
if ($LASTEXITCODE -ne 0) { throw "NativeElf development App Model host test build failed" }
& $output
if ($LASTEXITCODE -ne 0) { throw "NativeElf development App Model host test failed" }
