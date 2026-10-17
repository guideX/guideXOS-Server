$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $Root

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$outputDir = Join-Path $env:TEMP "guidex-navigator-javascript-js75-test-$stamp"
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
$testSource = "tests/navigator_javascript_js75_test.cpp"

$exe = Join-Path $outputDir "navigator_javascript_js75_test.exe"
$compileOutput = & g++ @baseFlags -O2 @diagnosticFlags $testSource $sources `
    -o $exe 2>&1
if ($LASTEXITCODE -ne 0) {
    $compileOutput | Write-Output
    throw "Navigator JavaScript JS75 focused test build failed."
}
$testOutput = & $exe 2>&1
$testExitCode = $LASTEXITCODE
$testOutput | ForEach-Object { Write-Output $_ }
if ($testExitCode -ne 0) {
    throw "Navigator JavaScript JS75 focused tests failed."
}

$strictOutput = & g++ @baseFlags @diagnosticFlags -fsyntax-only `
    $testSource $sources 2>&1
if ($LASTEXITCODE -ne 0) {
    $strictOutput | Write-Output
    throw "Navigator JavaScript JS75 strict compilation failed."
}
Write-Output "Navigator JavaScript JS75 strict compilation PASS"
$target = (& g++ -dumpmachine).Trim()
Write-Output "JS75 compiler target: $target"
Write-Output "JS75 compiler version: $(& g++ --version | Select-Object -First 1)"

if ($target -like "*mingw*") {
    $runtimeStackExe = Join-Path $outputDir "navigator_javascript_js75_stack_test.exe"
    $runtimeStackCompile = & g++ @baseFlags -O2 @diagnosticFlags `
        -DGXOS_JS74_STACK_INSTRUMENTED -DGXOS_JS75_STACK_PROBE `
        -finstrument-functions $testSource $sources `
        -o $runtimeStackExe 2>&1
    if ($LASTEXITCODE -ne 0) {
        $runtimeStackCompile | Write-Output
        throw "Navigator JavaScript JS75 instrumented stack test build failed."
    }
    $runtimeStackOutput = & $runtimeStackExe --stack-only 2>&1
    $runtimeStackExit = $LASTEXITCODE
    $runtimeStackOutput | ForEach-Object { Write-Output $_ }
    if ($runtimeStackExit -ne 0) {
        throw "Navigator JavaScript JS75 runtime stack workload failed."
    }
} else {
    Write-Output "JS75 runtime stack watermark unavailable: this runner requires a Windows MinGW target."
}

$stackDir = Join-Path $outputDir "stack-usage"
New-Item -ItemType Directory -Path $stackDir | Out-Null
foreach ($source in $sources) {
    $objectName = [System.IO.Path]::GetFileNameWithoutExtension($source) + ".o"
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $stackOutput = & g++ -std=c++17 -Wall -Wextra -O2 -iquote . `
        -fstack-usage -c $source -o (Join-Path $stackDir $objectName) 2>&1
    $stackExitCode = $LASTEXITCODE
    $ErrorActionPreference = $previousErrorActionPreference
    if ($stackExitCode -ne 0) {
        $stackOutput | Write-Output
        throw "JS75 stack-usage compilation failed for $source."
    }
}
$stackLines = @(Get-ChildItem -LiteralPath $stackDir -Filter "*.su" |
    ForEach-Object { Get-Content -LiteralPath $_.FullName })
$relevantFrames = @($stackLines | Where-Object {
    $_ -match "::callInternal\(" -or
    $_ -match "::selectorCollectionLookup\(" -or
    $_ -match "::selectorMatchAtInternal\(" -or
    $_ -match "::selectorMatchAtFilteredWithSlots" -or
    $_ -match "RuntimeContext::invokeHostMethod\("
})
if ($relevantFrames.Count -lt 4) {
    throw "JS75 stack report omitted the collection method or shared lookup frames."
}
Write-Output "JS75 production -O2 static stack frames:"
$relevantFrames | Sort-Object -Unique | ForEach-Object { Write-Output $_ }
Write-Output "Navigator JavaScript JS75 focused smoke PASS"
Write-Output "JS75 evidence output directory: $outputDir"
