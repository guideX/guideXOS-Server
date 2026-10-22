param(
    [string]$LogDirectory
)

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false
$Root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $Root

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
if ([string]::IsNullOrWhiteSpace($LogDirectory)) {
    $LogDirectory = $env:GXOS_NAVIGATOR_SMOKE_LOG_DIR
}
if ([string]::IsNullOrWhiteSpace($LogDirectory)) {
    $outputDir = Join-Path $env:TEMP ("guidex-navigator-javascript-js84-" +
        $stamp + "-" + [Guid]::NewGuid().ToString("N"))
} else {
    $outputDir = Join-Path ([System.IO.Path]::GetFullPath($LogDirectory)) `
        ("native-js84-" + $stamp + "-" + [Guid]::NewGuid().ToString("N"))
}
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$evidence = Join-Path $outputDir "js84-native-qualification.txt"

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
$lifetimeDiagnosticFlags = @("-DGXOS_JAVASCRIPT_LIFETIME_DIAGNOSTICS")
$testSource = "tests/navigator_javascript_js84_test.cpp"
$exe = Join-Path $outputDir "navigator_javascript_js84_test.exe"
$controlExe = Join-Path $outputDir "navigator_javascript_js84_no_lifetime_diagnostics.exe"

"JS84 strict flags: $($baseFlags -join ' ')" | Set-Content -LiteralPath $evidence
"Source revision: $(& git rev-parse HEAD)" | Add-Content -LiteralPath $evidence
"Compiler target: $((& g++ -dumpmachine).Trim())" | Add-Content -LiteralPath $evidence
"Compiler version: $(& g++ --version | Select-Object -First 1)" | Add-Content -LiteralPath $evidence

$compileOutput = & g++ @baseFlags -O2 @diagnosticFlags `
    @lifetimeDiagnosticFlags $testSource $sources `
    -o $exe 2>&1
if ($LASTEXITCODE -ne 0) {
    $compileOutput | ForEach-Object { Write-Output $_ }
    throw "Navigator JavaScript JS84 focused test build failed."
}
$compileOutput | ForEach-Object { Add-Content -LiteralPath $evidence -Value ([string]$_) }

$testOutput = & $exe 2>&1
$testExitCode = $LASTEXITCODE
$testOutput | ForEach-Object { Write-Output $_ }
$testOutput | ForEach-Object { Add-Content -LiteralPath $evidence -Value ([string]$_) }
if ($testExitCode -ne 0) {
    throw "Navigator JavaScript JS84 focused tests failed."
}

$strictOutput = & g++ @baseFlags @diagnosticFlags @lifetimeDiagnosticFlags `
    -fsyntax-only `
    $testSource $sources 2>&1
if ($LASTEXITCODE -ne 0) {
    $strictOutput | ForEach-Object { Write-Output $_ }
    throw "Navigator JavaScript JS84 strict compilation failed."
}
Write-Output "Navigator JavaScript JS84 strict compilation PASS"
"Strict compilation: PASS" | Add-Content -LiteralPath $evidence

$controlCompile = & g++ @baseFlags -O2 @diagnosticFlags $testSource $sources `
    -o $controlExe 2>&1
if ($LASTEXITCODE -ne 0) {
    $controlCompile | ForEach-Object { Write-Output $_ }
    throw "Navigator JavaScript JS84 no-diagnostics comparison build failed."
}
$controlOutput = & $controlExe 2>&1
$controlExitCode = $LASTEXITCODE
$controlOutput | ForEach-Object {
    Add-Content -LiteralPath $evidence -Value ([string]$_)
}
if ($controlExitCode -ne 0) {
    throw "Navigator JavaScript JS84 no-diagnostics comparison failed."
}
$benchmarkLabels = @(
    "for-of=", "forEach=", "values().next()=", "empty-loop=",
    "median 100 result-object allocations"
)
$instrumentedBenchmark = @($testOutput | Where-Object {
    $line = ([string]$_).Trim()
    @($benchmarkLabels | Where-Object { $line.StartsWith($_) }).Count -gt 0
})
$controlBenchmark = @($controlOutput | Where-Object {
    $line = ([string]$_).Trim()
    @($benchmarkLabels | Where-Object { $line.StartsWith($_) }).Count -gt 0
})
if ($instrumentedBenchmark.Count -ne $benchmarkLabels.Count -or
    $controlBenchmark.Count -ne $benchmarkLabels.Count) {
    throw "JS84 diagnostic-overhead comparison omitted a paired benchmark row."
}
$controlSummary = @($controlOutput | Where-Object {
    [string]$_ -match '^Navigator JavaScript JS84 checks:'
})
if ($controlSummary.Count -ne 1) {
    throw "JS84 no-diagnostics behavior comparison omitted its assertion summary."
}
Write-Output ("JS84 no-diagnostics compatibility run passed: " +
    ($controlSummary[0] -replace '^Navigator JavaScript JS84 checks:\s*', ''))
Write-Output "JS84 instrumentation-overhead comparison (medians of five fresh-context samples):"
for ($index = 0; $index -lt $benchmarkLabels.Count; ++$index) {
    Write-Output "  enabled:  $($instrumentedBenchmark[$index].Trim())"
    Write-Output "  disabled: $($controlBenchmark[$index].Trim())"
    "Instrumentation enabled benchmark: $($instrumentedBenchmark[$index].Trim())" |
        Add-Content -LiteralPath $evidence
    "Instrumentation disabled benchmark: $($controlBenchmark[$index].Trim())" |
        Add-Content -LiteralPath $evidence
}

$stackDir = Join-Path $outputDir "stack-usage"
New-Item -ItemType Directory -Path $stackDir | Out-Null
foreach ($source in $sources) {
    $objectName = [System.IO.Path]::GetFileNameWithoutExtension($source) + ".o"
    $stackOutput = & g++ -std=c++17 -Wall -Wextra -O2 `
        -Wno-unused-parameter -Wno-missing-field-initializers `
        -Wno-unused-function -iquote . @diagnosticFlags -fstack-usage `
        -c $source -o (Join-Path $stackDir $objectName) 2>&1
    if ($LASTEXITCODE -ne 0) {
        $stackOutput | ForEach-Object { Write-Output $_ }
        throw "JS84 stack-usage compilation failed for $source."
    }
}
$stackLines = @(Get-ChildItem -LiteralPath $stackDir -Filter "*.su" |
    ForEach-Object { Get-Content -LiteralPath $_.FullName })
$relevantFrames = @($stackLines | Where-Object {
    $_ -match "RuntimeContext::Evaluator::executeForOf\(" -or
    $_ -match "RuntimeContext::Evaluator::closeIterator\(" -or
    $_ -match "RuntimeContext::Evaluator::readValueProperty\(" -or
    $_ -match "RuntimeContext::Evaluator::invoke\(" -or
    $_ -match "selectorCollectionIteratorNext\(" -or
    $_ -match "RuntimeContext::invokeHostMethod\("
})
if ($relevantFrames.Count -lt 4) {
    throw "JS84 stack report omitted for-of, typed property, or iterator advancement frames."
}
Write-Output "JS84 production -O2 static stack frames:"
$relevantFrames | Sort-Object -Unique | ForEach-Object {
    Write-Output $_
    Add-Content -LiteralPath $evidence -Value ([string]$_)
}
Write-Output "Navigator JavaScript JS84 focused smoke PASS"
Write-Output "JS84 evidence directory: $outputDir"
"Evidence directory: $outputDir" | Add-Content -LiteralPath $evidence
