param(
    [string]$LogDirectory
)

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false
$Root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $Root

if ([string]::IsNullOrWhiteSpace($LogDirectory)) {
    $LogDirectory = $env:GXOS_NAVIGATOR_SMOKE_LOG_DIR
}
if ([string]::IsNullOrWhiteSpace($LogDirectory)) {
    $LogDirectory = Join-Path $env:TEMP "guidex-navigator-js86"
}
$baseLogDirectory = [System.IO.Path]::GetFullPath($LogDirectory)
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$outputDir = Join-Path $baseLogDirectory ("native-js86-" + $stamp + "-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$evidence = Join-Path $outputDir "js86-native-qualification.txt"
$testSource = "tests/navigator_javascript_js86_test.cpp"
$exe = Join-Path $outputDir "navigator_javascript_js86_test.exe"
$objectFile = Join-Path $outputDir "navigator_javascript_js86_test.o"

$baseFlags = @(
    "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
    "-iquote", "."
)
$compiler = Get-Command "g++" -ErrorAction SilentlyContinue
if (-not $compiler) {
    throw "g++ was not found on PATH."
}

@(
    "JS86 strict flags: $($baseFlags -join ' ')"
    "Source revision: $(& git rev-parse HEAD)"
    "Compiler path: $($compiler.Source)"
    "Compiler target: $((& g++ -dumpmachine).Trim())"
    "Compiler version: $(& g++ --version | Select-Object -First 1)"
) | Set-Content -LiteralPath $evidence

$strictOutput = & g++ @baseFlags -fsyntax-only $testSource 2>&1
if ($LASTEXITCODE -ne 0) {
    $strictOutput | ForEach-Object { Write-Output $_ }
    throw "Navigator JavaScript JS86 strict compilation failed."
}
$strictOutput | ForEach-Object {
    Add-Content -LiteralPath $evidence -Value ([string]$_)
}
"Strict compilation: PASS" | Add-Content -LiteralPath $evidence

$stackOutput = & g++ @baseFlags -O2 -fstack-usage -c $testSource -o $objectFile 2>&1
if ($LASTEXITCODE -ne 0) {
    $stackOutput | ForEach-Object { Write-Output $_ }
    throw "Navigator JavaScript JS86 stack-usage compilation failed."
}
$stackOutput | ForEach-Object {
    Add-Content -LiteralPath $evidence -Value ([string]$_)
}
$stackFile = [System.IO.Path]::ChangeExtension($objectFile, ".su")
if (-not (Test-Path -LiteralPath $stackFile)) {
    throw "Compiler did not emit the requested JS86 stack-usage file."
}
$stackRows = foreach ($line in Get-Content -LiteralPath $stackFile) {
    $columns = $line -split ([string][char]9)
    if ($columns.Count -ge 3 -and $columns[1] -match "^\d+$") {
        [pscustomobject]@{
            Function = $columns[0]
            Bytes = [int]$columns[1]
            Category = $columns[2]
        }
    }
}
$largestStack = $stackRows | Sort-Object Bytes -Descending | Select-Object -First 1
if (-not $largestStack) {
    throw "No stack-usage records were parsed from $stackFile."
}
"Largest static frame: $($largestStack.Bytes) bytes ($($largestStack.Function), $($largestStack.Category))" |
    Tee-Object -FilePath $evidence -Append

$linkOutput = & g++ @baseFlags -O2 $testSource -o $exe 2>&1
if ($LASTEXITCODE -ne 0) {
    $linkOutput | ForEach-Object { Write-Output $_ }
    throw "Navigator JavaScript JS86 native test link failed."
}
$linkOutput | ForEach-Object {
    Add-Content -LiteralPath $evidence -Value ([string]$_)
}
$testOutput = & $exe 2>&1
$testExitCode = $LASTEXITCODE
$testOutput | ForEach-Object { Write-Output $_ }
$testOutput | ForEach-Object {
    Add-Content -LiteralPath $evidence -Value ([string]$_)
}
if ($testExitCode -ne 0) {
    throw "Navigator JavaScript JS86 native proof checks failed."
}

"Native JS86 test: PASS" | Add-Content -LiteralPath $evidence
"Evidence directory: $outputDir" | Add-Content -LiteralPath $evidence
Write-Output "Navigator JavaScript JS86 strict compilation PASS"
Write-Output "Navigator JavaScript JS86 focused smoke PASS"
Write-Output "JS86 evidence directory: $outputDir"
