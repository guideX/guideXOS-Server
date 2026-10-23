$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false
$Root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $Root

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$baseLogDirectory = $env:GXOS_NAVIGATOR_SMOKE_LOG_DIR
if ([string]::IsNullOrWhiteSpace($baseLogDirectory)) {
    $baseLogDirectory = Join-Path $env:TEMP "guidex-navigator-js85"
}
$outputDir = Join-Path ([System.IO.Path]::GetFullPath($baseLogDirectory)) `
    ("native-js85-" + $stamp + "-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$evidence = Join-Path $outputDir "js85-native-qualification.txt"

$sources = @(
    "guide_web_html_parser.cpp",
    "navigator_javascript/value.cpp",
    "navigator_javascript/host.cpp",
    "navigator_javascript/environment.cpp",
    "navigator_javascript/runtime.cpp",
    "navigator_javascript/ast.cpp",
    "navigator_javascript/lexer.cpp",
    "navigator_javascript/parser.cpp",
    "navigator_javascript/navigator_script_host.cpp"
)
$baseFlags = @(
    "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
    "-Wno-unused-parameter", "-Wno-missing-field-initializers",
    "-Wno-unused-function", "-iquote", "."
)
$qualificationFlags = @(
    "-DGXOS_BARE_METAL", "-DGXOS_RELATIVE_SELECTOR_DIAGNOSTICS"
)
$testSource = "tests/navigator_javascript_js85_test.cpp"
$exe = Join-Path $outputDir "navigator_javascript_js85_test.exe"

"JS85 strict flags: $($baseFlags -join ' ')" | Set-Content -LiteralPath $evidence
"Source revision: $(& git rev-parse HEAD)" | Add-Content -LiteralPath $evidence
"Compiler target: $((& g++ -dumpmachine).Trim())" | Add-Content -LiteralPath $evidence
"Compiler version: $(& g++ --version | Select-Object -First 1)" | Add-Content -LiteralPath $evidence

$compileOutput = & g++ @baseFlags -O2 @qualificationFlags $testSource `
    $sources -o $exe 2>&1
if ($LASTEXITCODE -ne 0) {
    $compileOutput | ForEach-Object { Write-Output $_ }
    throw "Navigator JavaScript JS85 focused test build failed."
}
$compileOutput | ForEach-Object {
    Add-Content -LiteralPath $evidence -Value ([string]$_)
}

$testOutput = & $exe 2>&1
$testExitCode = $LASTEXITCODE
$testOutput | ForEach-Object { Write-Output $_ }
$testOutput | ForEach-Object {
    Add-Content -LiteralPath $evidence -Value ([string]$_)
}
if ($testExitCode -ne 0) {
    throw "Navigator JavaScript JS85 focused tests failed."
}

$strictOutput = & g++ @baseFlags @qualificationFlags -fsyntax-only `
    $testSource $sources 2>&1
if ($LASTEXITCODE -ne 0) {
    $strictOutput | ForEach-Object { Write-Output $_ }
    throw "Navigator JavaScript JS85 strict compilation failed."
}
Write-Output "Navigator JavaScript JS85 strict compilation PASS"
"Strict compilation: PASS" | Add-Content -LiteralPath $evidence
Write-Output "Navigator JavaScript JS85 focused smoke PASS"
Write-Output "JS85 evidence directory: $outputDir"
"Evidence directory: $outputDir" | Add-Content -LiteralPath $evidence
