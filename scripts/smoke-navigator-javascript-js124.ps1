param(
    [string]$LogDirectory
)

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false
$Root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $Root

if ([string]::IsNullOrWhiteSpace($LogDirectory)) {
    $LogDirectory = Join-Path $env:TEMP "guidex-navigator-js124"
}
$outputDir = Join-Path ([System.IO.Path]::GetFullPath($LogDirectory)) `
    ("native-js124-" + (Get-Date -Format "yyyyMMdd-HHmmss") + "-" +
        [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$exe = Join-Path $outputDir "navigator_javascript_js124_json_serializer_test.exe"
$evidence = Join-Path $outputDir "js124-json-serializer-qualification.txt"
@("JS124 shared portable JSON serializer qualification"
 "Source revision: $(& git rev-parse HEAD)"
 "Branch: $(& git branch --show-current)"
 "Compiler: $((Get-Command g++).Source)"
 "Compiler version: $((& g++ --version | Select-Object -First 1))"
 "Independent fixture engine: $(node --version)"
 "Fixture SHA256: $((Get-FileHash -LiteralPath 'tests/navigator_javascript_js124_number_expected.h' -Algorithm SHA256).Hash)"
 "Build flags: -std=c++17 -O2 -Wall -Wextra -Werror -pedantic") |
    Set-Content -LiteralPath $evidence

$build = & g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -iquote . `
    tests/navigator_javascript_js124_json_serializer_test.cpp `
    navigator_javascript/value.cpp `
    navigator_javascript/object_storage.cpp `
    -o $exe 2>&1
if ($LASTEXITCODE -ne 0) {
    $build | ForEach-Object { Write-Output $_ }
    throw "Navigator JavaScript JS124 focused test build failed."
}

$testOutput = & $exe 2>&1
$testExitCode = $LASTEXITCODE
$testOutput | Tee-Object -FilePath $evidence -Append
if ($testExitCode -ne 0) {
    throw "Navigator JavaScript JS124 focused checks failed."
}
Write-Output "Navigator JavaScript JS124 shared serializer smoke PASS"
Write-Output "JS124 evidence directory: $outputDir"
