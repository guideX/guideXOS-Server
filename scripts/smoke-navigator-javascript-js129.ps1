param(
    [string]$OutputDirectory
)

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false
$Root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $Root

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $env:TEMP ("guidex-navigator-js129-" +
        [Guid]::NewGuid().ToString("N"))
}
$Output = [System.IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $Output) {
    throw "Refusing to overwrite JS129 smoke evidence: $Output"
}
New-Item -ItemType Directory -Path $Output | Out-Null
$Exe = Join-Path $Output "navigator_javascript_js129_unified_call_entry_test.exe"
$Evidence = Join-Path $Output "js129-unified-call-entry-focused.txt"
$Stdout = Join-Path $Output "test.stdout.log"
$Stderr = Join-Path $Output "test.stderr.log"
$Compiler = (Get-Command g++ -ErrorAction Stop).Source

@(
    "JS129 reconstructed unified portable call entry qualification",
    "Base revision: $(& git rev-parse HEAD)",
    "Branch: $(& git branch --show-current)",
    "Working tree: $(& git status --short)",
    "Compiler: $Compiler",
    "Compiler version: $((& $Compiler --version | Select-Object -First 1))",
    "Build flags: -std=c++17 -O2 -Wall -Wextra -Werror -pedantic",
    "Test process exit code: pending"
) | Set-Content -LiteralPath $Evidence

$BuildOutput = & $Compiler -std=c++17 -O2 -Wall -Wextra -Werror -pedantic `
    -iquote . tests/navigator_javascript_js129_unified_call_entry_test.cpp `
    navigator_javascript/value.cpp `
    navigator_javascript/environment.cpp `
    navigator_javascript/object_storage.cpp `
    navigator_javascript/ast.cpp `
    navigator_javascript/lexer.cpp `
    navigator_javascript/parser.cpp -o $Exe 2>&1
$BuildExitCode = $LASTEXITCODE
if ($null -eq $BuildOutput -or @($BuildOutput).Count -eq 0) {
    Set-Content -LiteralPath (Join-Path $Output "build.log") -Value ""
} else {
    $BuildOutput | Set-Content -LiteralPath (Join-Path $Output "build.log")
}
if ($null -eq $BuildExitCode -or $BuildExitCode -ne 0) {
    Add-Content -LiteralPath $Evidence -Value "Build exit code: $BuildExitCode"
    throw "Navigator JavaScript JS129 focused test build failed."
}

$TestOutput = & $Exe 2> $Stderr
$TestExitCode = $LASTEXITCODE
$TestOutput | Set-Content -LiteralPath $Stdout
if ($null -eq $TestExitCode) {
    Add-Content -LiteralPath $Evidence -Value "Test process exit code: missing"
    throw "JS129 focused test process exit code was unavailable."
}
Add-Content -LiteralPath $Evidence -Value "Test process exit code: $TestExitCode"
$TestOutput | Tee-Object -FilePath $Evidence -Append
if ($TestExitCode -ne 0) {
    throw "Navigator JavaScript JS129 focused checks failed with exit code $TestExitCode."
}
Write-Output "Navigator JavaScript JS129 unified call entry smoke PASS"
Write-Output "JS129 evidence directory: $Output"
