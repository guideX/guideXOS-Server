param(
    [string]$OutputDirectory
)

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false
$Root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $Root

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $env:TEMP ("guidex-navigator-js128-" +
        [Guid]::NewGuid().ToString("N"))
}
$Output = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $Output -Force | Out-Null
$Exe = Join-Path $Output "navigator_javascript_js128_general_execution_test.exe"
$Evidence = Join-Path $Output "js128-general-resumable-focused.txt"
$Stdout = Join-Path $Output "test.stdout.log"
$Stderr = Join-Path $Output "test.stderr.log"
$Compiler = (Get-Command g++ -ErrorAction Stop).Source
@(
    "JS128 general resumable execution qualification",
    "Source revision: $(& git rev-parse HEAD)",
    "Branch: $(& git branch --show-current)",
    "Compiler: $Compiler",
    "Compiler version: $((& $Compiler --version | Select-Object -First 1))",
    "Build flags: -std=c++17 -O2 -Wall -Wextra -Werror -pedantic",
    "Test process exit code: pending"
) | Set-Content -LiteralPath $Evidence

$BuildOutput = & $Compiler -std=c++17 -O2 -Wall -Wextra -Werror -pedantic `
    -iquote . tests/navigator_javascript_js128_general_resumable_execution_test.cpp `
    navigator_javascript/value.cpp `
    navigator_javascript/environment.cpp `
    navigator_javascript/object_storage.cpp `
    navigator_javascript/ast.cpp `
    navigator_javascript/lexer.cpp `
    navigator_javascript/parser.cpp -o $Exe 2>&1
$BuildExitCode = $LASTEXITCODE
$BuildOutput | Set-Content -LiteralPath (Join-Path $Output "build.log")
if ($null -eq $BuildExitCode -or $BuildExitCode -ne 0) {
    Add-Content -LiteralPath $Evidence -Value "Build exit code: $BuildExitCode"
    throw "Navigator JavaScript JS128 focused test build failed."
}

$TestOutput = & $Exe 2> $Stderr
$TestExitCode = $LASTEXITCODE
$TestOutput | Set-Content -LiteralPath $Stdout
if ($null -eq $TestExitCode) {
    Add-Content -LiteralPath $Evidence -Value "Test process exit code: missing"
    throw "JS128 focused test process exit code was unavailable."
}
Add-Content -LiteralPath $Evidence -Value "Test process exit code: $TestExitCode"
$TestOutput | Tee-Object -FilePath $Evidence -Append
if ($TestExitCode -ne 0) {
    throw "Navigator JavaScript JS128 focused checks failed with exit code $TestExitCode."
}
Write-Output "Navigator JavaScript JS128 general resumable smoke PASS"
Write-Output "JS128 evidence directory: $Output"
