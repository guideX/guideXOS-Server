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
    $outputDir = Join-Path $env:TEMP ("guidex-navigator-javascript-js82-" +
        $stamp + "-" + [Guid]::NewGuid().ToString("N"))
} else {
    $outputDir = Join-Path ([System.IO.Path]::GetFullPath($LogDirectory)) `
        ("native-js82-" + $stamp + "-" + [Guid]::NewGuid().ToString("N"))
}
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$evidence = Join-Path $outputDir "js82-native-qualification.txt"

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
$testSource = "tests/navigator_javascript_js82_test.cpp"
$exe = Join-Path $outputDir "navigator_javascript_js82_test.exe"

"JS82 strict flags: $($baseFlags -join ' ')" | Set-Content -LiteralPath $evidence
"Source revision: $(& git rev-parse HEAD)" | Add-Content -LiteralPath $evidence
"Compiler target: $((& g++ -dumpmachine).Trim())" | Add-Content -LiteralPath $evidence
"Compiler version: $(& g++ --version | Select-Object -First 1)" | Add-Content -LiteralPath $evidence

$compileOutput = & g++ @baseFlags -O2 @diagnosticFlags $testSource $sources `
    -o $exe 2>&1
if ($LASTEXITCODE -ne 0) {
    $compileOutput | ForEach-Object { Write-Output $_ }
    throw "Navigator JavaScript JS82 focused test build failed."
}
$compileOutput | ForEach-Object { Add-Content -LiteralPath $evidence -Value ([string]$_) }

$testOutput = & $exe 2>&1
$testExitCode = $LASTEXITCODE
$testOutput | ForEach-Object { Write-Output $_ }
$testOutput | ForEach-Object { Add-Content -LiteralPath $evidence -Value ([string]$_) }
if ($testExitCode -ne 0) {
    throw "Navigator JavaScript JS82 focused tests failed."
}

$strictOutput = & g++ @baseFlags @diagnosticFlags -fsyntax-only `
    $testSource $sources 2>&1
if ($LASTEXITCODE -ne 0) {
    $strictOutput | ForEach-Object { Write-Output $_ }
    throw "Navigator JavaScript JS82 strict compilation failed."
}
Write-Output "Navigator JavaScript JS82 strict compilation PASS"
"Strict compilation: PASS" | Add-Content -LiteralPath $evidence

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
        throw "JS82 stack-usage compilation failed for $source."
    }
}
$stackLines = @(Get-ChildItem -LiteralPath $stackDir -Filter "*.su" |
    ForEach-Object { Get-Content -LiteralPath $_.FullName })
$relevantFrames = @($stackLines | Where-Object {
    $_ -match "resolveMember\(" -or
    $_ -match "readMember\(" -or
    $_ -match "readHostProperty\(" -or
    $_ -match "getSymbolProperty\(" -or
    $_ -match "selectorCollectionIteratorNext\(" -or
    $_ -match "selectorCollectionIteratorCreate\(" -or
    $_ -match "RuntimeContext::invokeHostMethod\(" -or
    $_ -match "RuntimeContext::Evaluator::invoke\(" -or
    $_ -match "NavigatorScriptHostAdapter::callInternal\("
})
if ($relevantFrames.Count -lt 5) {
    throw "JS82 stack report omitted typed Symbol lookup or host-method dispatch frames."
}
Write-Output "JS82 production -O2 static stack frames:"
$relevantFrames | Sort-Object -Unique | ForEach-Object {
    Write-Output $_
    Add-Content -LiteralPath $evidence -Value ([string]$_)
}
Write-Output "Navigator JavaScript JS82 focused smoke PASS"
Write-Output "JS82 evidence directory: $outputDir"
"Evidence directory: $outputDir" | Add-Content -LiteralPath $evidence
