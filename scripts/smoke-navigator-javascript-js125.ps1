param(
    [string]$OutputDirectory
)

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false
$Root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $Root

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $env:TEMP ("guidex-navigator-js125-" +
        [Guid]::NewGuid().ToString("N"))
}
$Output = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $Output -Force | Out-Null
$Exe = Join-Path $Output "navigator_javascript_js125_callback_foundation_test.exe"
$Evidence = Join-Path $Output "js125-callback-foundation-focused.txt"
$Compiler = (Get-Command g++ -ErrorAction Stop).Source
@(
    "JS125 shared portable callback foundation focused qualification",
    "Source revision: $(& git rev-parse HEAD)",
    "Branch: $(& git branch --show-current)",
    "Compiler: $Compiler",
    "Compiler version: $((& $Compiler --version | Select-Object -First 1))",
    "Build flags: -std=c++17 -O2 -Wall -Wextra -Werror -pedantic"
) | Set-Content -LiteralPath $Evidence

$Build = & $Compiler -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -iquote . `
    tests/navigator_javascript_js125_callback_foundation_test.cpp `
    navigator_javascript/value.cpp `
    navigator_javascript/environment.cpp `
    navigator_javascript/runtime.cpp `
    navigator_javascript/object_storage.cpp `
    navigator_javascript/ast.cpp `
    navigator_javascript/lexer.cpp `
    navigator_javascript/parser.cpp -o $Exe 2>&1
if ($LASTEXITCODE -ne 0) {
    $Build | ForEach-Object { Write-Output $_ }
    throw "Navigator JavaScript JS125 focused test build failed."
}

$TestOutput = & $Exe 2>&1
$TestExitCode = $LASTEXITCODE
$TestOutput | Tee-Object -FilePath $Evidence -Append
if ($TestExitCode -ne 0) {
    throw "Navigator JavaScript JS125 focused checks failed."
}
Write-Output "Navigator JavaScript JS125 callback-foundation smoke PASS"
Write-Output "JS125 evidence directory: $Output"
