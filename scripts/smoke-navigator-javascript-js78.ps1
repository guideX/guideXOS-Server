$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false
$Root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $Root

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$outputDir = Join-Path $env:TEMP ("guidex-navigator-javascript-js78-test-" + $stamp + "-" + [Guid]::NewGuid().ToString("N"))
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
$testSource = "tests/navigator_javascript_js78_test.cpp"
$exe = Join-Path $outputDir "navigator_javascript_js78_test.exe"

$compileOutput = & g++ @baseFlags -O2 @diagnosticFlags $testSource $sources `
    -o $exe 2>&1
if ($LASTEXITCODE -ne 0) {
    $compileOutput | ForEach-Object { Write-Output $_ }
    throw "Navigator JavaScript JS78 focused test build failed."
}
$testOutput = & $exe 2>&1
$testExitCode = $LASTEXITCODE
$testOutput | ForEach-Object { Write-Output $_ }
if ($testExitCode -ne 0) {
    throw "Navigator JavaScript JS78 focused tests failed."
}

$strictOutput = & g++ @baseFlags @diagnosticFlags -fsyntax-only `
    $testSource $sources 2>&1
if ($LASTEXITCODE -ne 0) {
    $strictOutput | ForEach-Object { Write-Output $_ }
    throw "Navigator JavaScript JS78 strict compilation failed."
}
Write-Output "Navigator JavaScript JS78 strict compilation PASS"
$target = (& g++ -dumpmachine).Trim()
Write-Output "JS78 compiler target: $target"
Write-Output "JS78 compiler version: $(& g++ --version | Select-Object -First 1)"

if ($target -like "*mingw*") {
    $runtimeStackExe = Join-Path $outputDir "navigator_javascript_js78_stack_test.exe"
    $runtimeStackCompile = & g++ @baseFlags -O2 @diagnosticFlags `
        -DGXOS_JS78_STACK_INSTRUMENTED -finstrument-functions `
        $testSource $sources -o $runtimeStackExe 2>&1
    if ($LASTEXITCODE -ne 0) {
        $runtimeStackCompile | ForEach-Object { Write-Output $_ }
        throw "Navigator JavaScript JS78 instrumented stack test build failed."
    }
    $runtimeStackOutput = & $runtimeStackExe --stack-only 2>&1
    $runtimeStackExit = $LASTEXITCODE
    $runtimeStackOutput | ForEach-Object { Write-Output $_ }
    if ($runtimeStackExit -ne 0) {
        throw "Navigator JavaScript JS78 runtime stack workload failed."
    }
} else {
    Write-Output "JS78 runtime stack watermark unavailable: this runner requires a Windows MinGW target."
}

$stackDir = Join-Path $outputDir "stack-usage"
New-Item -ItemType Directory -Path $stackDir | Out-Null
foreach ($source in $sources) {
    $objectName = [System.IO.Path]::GetFileNameWithoutExtension($source) + ".o"
    $stackOutput = & g++ -std=c++17 -Wall -Wextra -O2 -iquote . `
        -fstack-usage -c $source -o (Join-Path $stackDir $objectName) 2>&1
    if ($LASTEXITCODE -ne 0) {
        $stackOutput | ForEach-Object { Write-Output $_ }
        throw "JS78 stack-usage compilation failed for $source."
    }
}
$stackLines = @(Get-ChildItem -LiteralPath $stackDir -Filter "*.su" |
    ForEach-Object { Get-Content -LiteralPath $_.FullName })
$relevantFrames = @($stackLines | Where-Object {
    $_ -match "::selectorCollectionCallbackTraversal\(" -or
    $_ -match "::selectorCollectionEvery\(" -or
    $_ -match "RuntimeContext::invokeHostMethod\(" -or
    $_ -match "RuntimeContext::Evaluator::invoke\(" -or
    $_ -match "RuntimeContext::Evaluator::invokeFunction\(" -or
    $_ -match "::selectorCollectionLookup\(" -or
    $_ -match "::selectorMatchAtFilteredWithSlots"
})
if ($relevantFrames.Count -lt 5) {
    throw "JS78 stack report omitted callback dispatch or collection lookup frames."
}
Write-Output "JS78 production -O2 static stack frames:"
$relevantFrames | Sort-Object -Unique | ForEach-Object { Write-Output $_ }
Write-Output "Navigator JavaScript JS78 focused smoke PASS"
Write-Output "JS78 evidence output directory: $outputDir"
