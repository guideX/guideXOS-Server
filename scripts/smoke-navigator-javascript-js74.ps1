$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $Root

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$outputDir = Join-Path $env:TEMP "guidex-navigator-javascript-js74-test-$stamp"
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
$testSource = "tests/navigator_javascript_js74_test.cpp"

$exe = Join-Path $outputDir "navigator_javascript_js74_test.exe"
$compileOutput = & g++ @baseFlags -O2 @diagnosticFlags $testSource $sources `
    -o $exe 2>&1
if ($LASTEXITCODE -ne 0) {
    $compileOutput | Write-Output
    throw "Navigator JavaScript JS74 focused test build failed."
}
$testOutput = & $exe 2>&1
$testExitCode = $LASTEXITCODE
$testOutput | ForEach-Object { Write-Output $_ }
if ($testExitCode -ne 0) {
    throw "Navigator JavaScript JS74 focused tests or benchmark checks failed."
}

$strictOutput = & g++ @baseFlags @diagnosticFlags -fsyntax-only `
    $testSource $sources 2>&1
if ($LASTEXITCODE -ne 0) {
    $strictOutput | Write-Output
    throw "Navigator JavaScript JS74 strict compilation failed."
}
Write-Output "Navigator JavaScript JS74 strict compilation PASS"

$target = (& g++ -dumpmachine).Trim()
Write-Output "JS74 compiler target: $target"
Write-Output "JS74 compiler version: $(& g++ --version | Select-Object -First 1)"

$stackExe = Join-Path $outputDir "navigator_javascript_js74_stack_test.exe"
if ($target -like "*mingw*") {
    $stackCompileOutput = & g++ @baseFlags -O2 @diagnosticFlags `
        -DGXOS_JS74_STACK_INSTRUMENTED -finstrument-functions `
        $testSource $sources -o $stackExe 2>&1
    if ($LASTEXITCODE -ne 0) {
        $stackCompileOutput | Write-Output
        throw "Navigator JavaScript JS74 instrumented stack test build failed."
    }
    $stackOutput = & $stackExe --stack-only 2>&1
    $stackExitCode = $LASTEXITCODE
    $stackOutput | ForEach-Object { Write-Output $_ }
    if ($stackExitCode -ne 0) {
        throw "Navigator JavaScript JS74 runtime stack workload failed."
    }
} else {
    Write-Output "JS74 runtime stack watermark unavailable: this runner requires a Windows MinGW target."
}

$reportRoot = Join-Path $outputDir "stack-usage"
$productionDir = Join-Path $reportRoot "production"
$nativeDir = Join-Path $reportRoot "native-test"
New-Item -ItemType Directory -Force -Path $productionDir | Out-Null
New-Item -ItemType Directory -Force -Path $nativeDir | Out-Null

function Write-StackReports {
    param(
        [string]$Destination,
        [switch]$NativeTest
    )

    foreach ($source in $sources) {
        $objectName = [System.IO.Path]::GetFileNameWithoutExtension($source) + ".o"
        $arguments = @("-std=c++17", "-Wall", "-Wno-unused-function", "-O2",
            "-iquote", ".", "-fstack-usage")
        if ($NativeTest) {
            $arguments += $diagnosticFlags
        }
        $arguments += @("-c", $source, "-o", (Join-Path $Destination $objectName))
        $reportCompile = & g++ @arguments 2>&1
        if ($LASTEXITCODE -ne 0) {
            $reportCompile | Write-Output
            throw "JS74 -fstack-usage compile failed for $source."
        }
    }
}

Write-StackReports -Destination $productionDir
Write-StackReports -Destination $nativeDir -NativeTest

foreach ($mode in @(
    @{ Label = "production -std=c++17 -Wall -O2 -iquote ."; Path = $productionDir },
    @{ Label = "native -std=c++17 -Wall -O2 -DGXOS_BARE_METAL -DGXOS_RELATIVE_SELECTOR_DIAGNOSTICS"; Path = $nativeDir }
)) {
    Write-Output "JS74 stack report: $($mode.Label)"
    $reportFiles = Get-ChildItem -LiteralPath $mode.Path -Filter "*.su"
    $lines = foreach ($reportFile in $reportFiles) {
        Get-Content -LiteralPath $reportFile.FullName
    }
    $relevant = @($lines | Where-Object {
        $_ -match "selectorMatch(Count|At)FilteredWithSlots" -and
        $_ -match 'RankSlotCount = (1|4|8)\]'
    })
    $relevant += @($lines | Where-Object {
        $_ -match '::dispatchEvent\(' -or
        $_ -match '::callInternal\(' -or
        $_ -match '::getProperty\(' -or
        $_ -match '::querySelector(All)?\(' -or
        $_ -match '::selectorMatch(Count|At)(Internal|Filtered)?\(' -or
        $_ -match '::selector(Element|MemberElement|SimpleElement)Matches\(' -or
        $_ -match '::selectorSimpleElementMatchResult\(' -or
        $_ -match '::resolveFilteredStructuralIndex\(' -or
        $_ -match '::buildFilteredNthRankCache\(' -or
        $_ -match '::selectorCore(Element|Conditions)MatchResult\(' -or
        $_ -match 'parse(BoundedSelector(Member)?|SimpleSelector)\(' -or
        $_ -match 'RuntimeContext::execute\(' -or
        $_ -match 'Evaluator::(run|executeStatement|evalCall|evalBinary)\(' -or
        $_ -match 'Evaluator::invokeFunction\(' -or
        $_ -match 'RuntimeContext::(readHostProperty|invokeHostMethod)\(' -or
        $_ -match 'Parser::parse\('
    })
    if ($relevant.Count -lt 10) {
        throw "JS74 stack report omitted expected selector or caller frames."
    }
    $relevant | Sort-Object -Unique | ForEach-Object { Write-Output $_ }
    $classifications = @($lines | ForEach-Object {
        $parts = $_ -split "`t"
        if ($parts.Count -ge 3) { $parts[-1] }
    } | Sort-Object -Unique)
    Write-Output "JS74 stack usage classifications: $($classifications -join ', ')"
}

Write-Output "Navigator JavaScript JS74 focused smoke PASS"
Write-Output "JS74 evidence output directory: $outputDir"
