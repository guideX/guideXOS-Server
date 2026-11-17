param(
    [string]$LogDirectory
)

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false
$Root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $Root

if ([string]::IsNullOrWhiteSpace($LogDirectory)) {
    $LogDirectory = Join-Path $env:TEMP "guidex-navigator-js123"
}
$outputDir = Join-Path ([System.IO.Path]::GetFullPath($LogDirectory)) `
    ("native-js123-" + (Get-Date -Format "yyyyMMdd-HHmmss") + "-" +
        [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$exe = Join-Path $outputDir "navigator_javascript_js123_numeric_test.exe"
$evidence = Join-Path $outputDir "js123-numeric-writer-qualification.txt"
@("JS123 portable JSON numeric conversion and bounded writer qualification"
 "Source revision: $(& git rev-parse HEAD)"
 "Compiler: $((Get-Command g++).Source)"
 "Build flags: -std=c++17 -O2 -Wall -Wextra -Werror -pedantic") |
    Set-Content -LiteralPath $evidence

$build = & g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -iquote . `
    tests/navigator_javascript_js123_numeric_test.cpp `
    navigator_javascript/value.cpp `
    navigator_javascript/object_storage.cpp -o $exe 2>&1
if ($LASTEXITCODE -ne 0) {
    $build | ForEach-Object { Write-Output $_ }
    throw "Navigator JavaScript JS123 focused test build failed."
}

$testOutput = & $exe 2>&1
$testExitCode = $LASTEXITCODE
$testOutput | Tee-Object -FilePath $evidence -Append
if ($testExitCode -ne 0) {
    throw "Navigator JavaScript JS123 focused checks failed."
}
Write-Output "Navigator JavaScript JS123 numeric/writer smoke PASS"
Write-Output "JS123 evidence directory: $outputDir"
