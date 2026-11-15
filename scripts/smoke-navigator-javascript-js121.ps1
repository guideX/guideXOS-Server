param(
    [string]$LogDirectory
)

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false
$Root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $Root

if ([string]::IsNullOrWhiteSpace($LogDirectory)) {
    $LogDirectory = Join-Path $env:TEMP "guidex-navigator-js121"
}
$outputDir = Join-Path ([System.IO.Path]::GetFullPath($LogDirectory)) `
    ("native-js121-" + (Get-Date -Format "yyyyMMdd-HHmmss") + "-" +
        [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$exe = Join-Path $outputDir "navigator_javascript_js121_test.exe"
$evidence = Join-Path $outputDir "js121-hosted-qualification.txt"
@("JS121 hosted shared object-store qualification"
 "Source revision: $(& git rev-parse HEAD)"
 "Compiler: $((Get-Command g++).Source)"
 "Build flags: -std=c++17 -O2 -Wall -Wextra -Werror -pedantic") |
    Set-Content -LiteralPath $evidence

$build = & g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -iquote . `
    tests/navigator_javascript_js121_test.cpp `
    navigator_javascript/value.cpp `
    navigator_javascript/environment.cpp `
    navigator_javascript/runtime.cpp `
    navigator_javascript/object_storage.cpp `
    navigator_javascript/ast.cpp `
    navigator_javascript/lexer.cpp `
    navigator_javascript/parser.cpp -o $exe 2>&1
if ($LASTEXITCODE -ne 0) {
    $build | ForEach-Object { Write-Output $_ }
    throw "Navigator JavaScript JS121 hosted test build failed."
}

$testOutput = & $exe 2>&1
$testExitCode = $LASTEXITCODE
$testOutput | Tee-Object -FilePath $evidence -Append
if ($testExitCode -ne 0) {
    throw "Navigator JavaScript JS121 hosted shared-store checks failed."
}
Write-Output "Navigator JavaScript JS121 smoke PASS"
