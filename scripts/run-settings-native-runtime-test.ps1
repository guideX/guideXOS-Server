param(
    [string]$OutputDirectory = "out\validation\settings-s11-native-runtime-20261011"
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $RepoRoot

$Compiler = Get-Command g++ -ErrorAction Stop
$FullOutputDirectory = Join-Path $RepoRoot $OutputDirectory
New-Item -ItemType Directory -Path $FullOutputDirectory -Force | Out-Null
$Output = Join-Path $FullOutputDirectory "settings_native_runtime_test.exe"

$Sources = @(
    "tests/settings_native_runtime_test.cpp",
    "kernel/core/kernel_app.cpp",
    "kernel/core/app_launch_target_resolver.cpp",
    "kernel/core/settings_native_provider.cpp",
    "kernel/core/settings_display_refresh.cpp",
    "settings_presentation_core.cpp"
)
$Arguments = @(
    "-std=c++14", "-O2", "-Wall", "-Wextra", "-fno-exceptions", "-fno-rtti",
    "-DGXOS_BARE_METAL", "-iquote", ".", "-idirafter", "kernel/core/include",
    "-Ikernel/arch/amd64/include"
) + $Sources + @("-o", $Output)

& $Compiler.Source @Arguments
if ($LASTEXITCODE -ne 0) { throw "Native Settings runtime test build failed with exit code $LASTEXITCODE." }

& $Output
if ($LASTEXITCODE -ne 0) { throw "Native Settings runtime tests failed with exit code $LASTEXITCODE." }
Write-Host "Native Settings runtime build and tests PASS. Output: $Output"
