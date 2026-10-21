$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false
$Root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $Root

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$outputDir = Join-Path $env:TEMP ("guidex-navigator-javascript-js81-test-" + $stamp + "-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $outputDir | Out-Null
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
$diagnosticFlags = @(
    "-DGXOS_BARE_METAL", "-DGXOS_RELATIVE_SELECTOR_DIAGNOSTICS"
)
$testSource = "tests/navigator_javascript_js81_test.cpp"
$exe = Join-Path $outputDir "navigator_javascript_js81_test.exe"

$compileOutput = & g++ @baseFlags -O2 @diagnosticFlags $testSource $sources `
    -o $exe 2>&1
if ($LASTEXITCODE -ne 0) {
    $compileOutput | ForEach-Object { Write-Output $_ }
    throw "Navigator JavaScript JS81 focused test build failed."
}
$testOutput = & $exe 2>&1
$testExitCode = $LASTEXITCODE
$testOutput | ForEach-Object { Write-Output $_ }
if ($testExitCode -ne 0) {
    throw "Navigator JavaScript JS81 focused tests failed."
}

$strictOutput = & g++ @baseFlags @diagnosticFlags -fsyntax-only `
    $testSource $sources 2>&1
if ($LASTEXITCODE -ne 0) {
    $strictOutput | ForEach-Object { Write-Output $_ }
    throw "Navigator JavaScript JS81 strict compilation failed."
}
Write-Output "Navigator JavaScript JS81 strict compilation PASS"
Write-Output "JS81 compiler target: $((& g++ -dumpmachine).Trim())"
Write-Output "JS81 compiler version: $(& g++ --version | Select-Object -First 1)"

$stackDir = Join-Path $outputDir "stack-usage"
New-Item -ItemType Directory -Path $stackDir | Out-Null
foreach ($source in $sources) {
    $objectName = [System.IO.Path]::GetFileNameWithoutExtension($source) + ".o"
    $stackOutput = & g++ -std=c++17 -Wall -Wextra -O2 `
        -Wno-unused-parameter -Wno-missing-field-initializers `
        -Wno-unused-function -iquote . `
        @diagnosticFlags -fstack-usage -c $source `
        -o (Join-Path $stackDir $objectName) 2>&1
    if ($LASTEXITCODE -ne 0) {
        $stackOutput | ForEach-Object { Write-Output $_ }
        throw "JS81 stack-usage compilation failed for $source."
    }
}
$stackLines = @(Get-ChildItem -LiteralPath $stackDir -Filter "*.su" |
    ForEach-Object { Get-Content -LiteralPath $_.FullName })
$relevantFrames = @($stackLines | Where-Object {
    $_ -match "selectorCollectionIteratorNext\(" -or
    $_ -match "selectorCollectionIteratorCreate\(" -or
    $_ -match "selectorCollectionValues\(" -or
    $_ -match "selectorCollectionKeys\(" -or
    $_ -match "selectorCollectionEntries\(" -or
    $_ -match "createCollectionIteratorResult\(" -or
    $_ -match "createCollectionIteratorEntryResult\(" -or
    $_ -match "selectorMatchAtFilteredWithSlots" -or
    $_ -match "RuntimeContext::invokeHostMethod\(" -or
    $_ -match "RuntimeContext::Evaluator::invoke\("
})
if ($relevantFrames.Count -lt 5) {
    throw "JS81 stack report omitted iterator dispatch or live selector lookup frames."
}
Write-Output "JS81 production -O2 static stack frames:"
$relevantFrames | Sort-Object -Unique | ForEach-Object { Write-Output $_ }
Write-Output "Navigator JavaScript JS81 focused smoke PASS"
Write-Output "JS81 evidence output directory: $outputDir"
