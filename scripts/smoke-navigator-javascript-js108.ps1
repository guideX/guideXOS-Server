param(
    [string]$LogDirectory,
    [switch]$SkipStackUsage
)

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false
$Root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $Root

if ([string]::IsNullOrWhiteSpace($LogDirectory)) {
    $LogDirectory = $env:GXOS_NAVIGATOR_SMOKE_LOG_DIR
}
if ([string]::IsNullOrWhiteSpace($LogDirectory)) {
    $LogDirectory = Join-Path $env:LOCALAPPDATA "Temp\guidex-js108-evidence"
}
$baseLogDirectory = [System.IO.Path]::GetFullPath($LogDirectory)
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$outputDir = Join-Path $baseLogDirectory ("native-js108-" + $stamp + "-" +
    [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$evidence = Join-Path $outputDir "js108-native-qualification.txt"
$testSource = "tests/navigator_javascript_js108_test.cpp"
$exe = Join-Path $outputDir "navigator_javascript_js108_test.exe"
$stackDir = Join-Path $outputDir "stack-usage"
New-Item -ItemType Directory -Path $stackDir | Out-Null

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
$stackSources = $sources + @("navigator.cpp")
$baseFlags = @(
    "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic", "-iquote", "."
)
$qualificationFlags = @(
    "-DGXOS_BARE_METAL", "-DGXOS_RELATIVE_SELECTOR_DIAGNOSTICS",
    "-DGXOS_JAVASCRIPT_LIFETIME_DIAGNOSTICS",
    "-DGXOS_JAVASCRIPT_TEST_FAILURE_INJECTION"
)
$compiler = Get-Command "g++" -ErrorAction SilentlyContinue
if (-not $compiler) { throw "g++ was not found on PATH." }

@(
    "JS108 strict flags: $($baseFlags -join ' ')"
    "Source revision: $(& git rev-parse HEAD)"
    "Branch: $(& git branch --show-current)"
    "Compiler path: $($compiler.Source)"
    "Compiler target: $((& g++ -dumpmachine).Trim())"
    "Compiler version: $(& g++ --version | Select-Object -First 1)"
    "Fixture SHA256: $((Get-FileHash -LiteralPath 'navigator-smoke/javascript-js108.html' -Algorithm SHA256).Hash)"
) | Set-Content -LiteralPath $evidence

$strictOutput = & g++ @baseFlags @qualificationFlags -fsyntax-only `
    $testSource "navigator_javascript/runtime.cpp" `
    "navigator_javascript/ast.cpp" "navigator_javascript/parser.cpp" `
    "navigator_javascript/lexer.cpp" "navigator_javascript/environment.cpp" 2>&1
if ($LASTEXITCODE -ne 0) {
    $strictOutput | ForEach-Object { Write-Output $_ }
    throw "Navigator JavaScript JS108 strict C++17 compilation failed."
}
$strictOutput | ForEach-Object {
    Add-Content -LiteralPath $evidence -Value ([string]$_)
}
"JS108 focused test, lexer, parser, AST, environment, and runtime strict compile: PASS" |
    Add-Content -LiteralPath $evidence

$linkFlags = $baseFlags + @(
    "-Wno-unused-parameter", "-Wno-missing-field-initializers",
    "-Wno-unused-function"
)
$compileOutput = & g++ @linkFlags -O2 @qualificationFlags `
    $testSource $sources -o $exe 2>&1
if ($LASTEXITCODE -ne 0) {
    $compileOutput | ForEach-Object { Write-Output $_ }
    throw "Navigator JavaScript JS108 focused test link failed."
}
$compileOutput | ForEach-Object {
    Add-Content -LiteralPath $evidence -Value ([string]$_)
}

$nativeLog = Join-Path $outputDir "js108-native-output.txt"
$nativeCommand = '"' + $exe + '" > "' + $nativeLog + '" 2>&1'
& $env:ComSpec /d /c $nativeCommand
$testExitCode = $LASTEXITCODE
$testOutput = @(Get-Content -LiteralPath $nativeLog)
$testOutput | ForEach-Object { Write-Output $_ }
$testOutput | ForEach-Object {
    Add-Content -LiteralPath $evidence -Value ([string]$_)
}
if ($testExitCode -ne 0) {
    throw "Navigator JavaScript JS108 native qualification failed."
}
$summary = @($testOutput | Where-Object {
    [string]$_ -match '^Navigator JavaScript JS108 checks:'
})
if ($summary.Count -ne 1) {
    throw "JS108 test output omitted its assertion total."
}
$summary[0] | Add-Content -LiteralPath $evidence

if (-not $SkipStackUsage) {
    foreach ($source in $stackSources) {
        $objectName = [System.IO.Path]::GetFileNameWithoutExtension($source) + ".o"
        $objectPath = Join-Path $stackDir $objectName
        $stackCompileFlags = $linkFlags
        $stackCompileDefines = $qualificationFlags
        if ($source -eq "navigator.cpp") {
            $stackCompileFlags = $baseFlags + @(
                "-Wno-unused-parameter", "-Wno-missing-field-initializers",
                "-Wno-unused-function", "-Wno-comment",
                "-Wno-misleading-indentation", "-Wno-sign-compare"
            )
            $stackCompileDefines = @()
        }
        $stackOutput = & g++ @stackCompileFlags -O2 @stackCompileDefines `
            -fstack-usage -c $source -o $objectPath 2>&1
        if ($LASTEXITCODE -ne 0) {
            $stackOutput | ForEach-Object { Write-Output $_ }
            throw "JS108 stack-usage compilation failed for $source."
        }
    }
    $stackRows = @(Get-ChildItem -LiteralPath $stackDir -Filter "*.su" |
        ForEach-Object { Get-Content -LiteralPath $_.FullName } | Where-Object {
        $_ -match "(insertDynamicChild\(|appendDynamicChild\(|moveSubtreeInlineItems\(|rebuildDynamicBlockProjections\(|documentOrderElements\(|refreshParentChildMetadata\(|setSubtreeConnectionAndFlow\(|rebuildInlineLayout\(|Navigator::renderDocument\()"
        })
    foreach ($required in @("insertDynamicChild\(", "moveSubtreeInlineItems\(",
            "rebuildDynamicBlockProjections\(", "documentOrderElements\(",
            "refreshParentChildMetadata\(", "rebuildInlineLayout\(",
            "Navigator::renderDocument\(")) {
        if (@($stackRows | Where-Object { $_ -match $required }).Count -eq 0) {
            throw "JS108 stack report omitted $required."
        }
    }
    "JS108 optimized dynamic DOM and projection static stack frames:" |
        Tee-Object -FilePath $evidence -Append
    $stackRows | Sort-Object -Unique | ForEach-Object {
        Write-Output $_
        Add-Content -LiteralPath $evidence -Value ([string]$_)
    }
} else {
    "JS108 static stack-usage compilation skipped by request." |
        Add-Content -LiteralPath $evidence
}

"Native JS108 test: PASS" | Add-Content -LiteralPath $evidence
"Evidence directory: $outputDir" | Add-Content -LiteralPath $evidence
Write-Output "Navigator JavaScript JS108 strict compilation PASS"
Write-Output "Navigator JavaScript JS108 focused smoke PASS"
Write-Output "JS108 evidence directory: $outputDir"
