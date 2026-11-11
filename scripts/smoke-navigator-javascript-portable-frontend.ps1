param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot),
    [string]$OutputDirectory = (Join-Path $env:TEMP "navigator-js115-portable-frontend"),
    [string]$TargetCompiler = "x86_64-w64-mingw32-g++"
)

$ErrorActionPreference = "Stop"
Set-Location -LiteralPath $Root
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

$targetCommand = Get-Command $TargetCompiler -ErrorAction Stop
$targetCompilerPath = $targetCommand.Source
$toolchainRoot = Split-Path -Parent (Split-Path -Parent $targetCompilerPath)
$targetVersion = (& $targetCompilerPath -dumpversion).Trim()
$targetTriple = (& $targetCompilerPath -dumpmachine).Trim()
$cxxInclude = Join-Path $toolchainRoot "include\c++\$targetVersion"
$targetCxxInclude = Join-Path $cxxInclude $targetTriple
if (-not (Test-Path -LiteralPath (Join-Path $cxxInclude "cstddef")) -or
    -not (Test-Path -LiteralPath $targetCxxInclude)) {
    throw "Could not locate the C++17 freestanding header directories for $targetCompilerPath."
}

$hostCompiler = (Get-Command "g++" -ErrorAction Stop).Source
$hostExecutable = Join-Path $OutputDirectory "portable_frontend_test.exe"
$hostLog = Join-Path $OutputDirectory "portable-host-build-and-run.log"
$hostArguments = @(
    "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
    "-DGXOS_NAVIGATOR_FRONTEND_FREESTANDING", "-iquote", ".",
    "tests/navigator_javascript_portable_lexer_test.cpp",
    "navigator_javascript/lexer.cpp", "navigator_javascript/ast.cpp",
    "-o", $hostExecutable
)
$hostOutput = & $hostCompiler @hostArguments 2>&1
$hostExit = $LASTEXITCODE
$hostOutput | Set-Content -LiteralPath $hostLog -Encoding UTF8
if ($hostExit -ne 0) { throw "Portable frontend hosted compile failed; see $hostLog" }
$hostOutput = & $hostExecutable 2>&1
$hostExit = $LASTEXITCODE
$hostOutput | Add-Content -LiteralPath $hostLog -Encoding UTF8
if ($hostExit -ne 0) { throw "Portable frontend shared-core checks failed; see $hostLog" }

$targetFlags = @(
    "-std=c++17", "-ffreestanding", "-O2", "-Wall", "-Wextra", "-Werror",
    "-pedantic", "-fno-exceptions", "-fno-rtti", "-nostdlib", "-nostdinc++",
    "-fno-builtin", "-DGXOS_BARE_METAL",
    "-DGXOS_NAVIGATOR_FRONTEND_FREESTANDING",
    "-isystem", $cxxInclude, "-isystem", $targetCxxInclude,
    "-I", (Join-Path $Root "kernel"), "-I", $Root,
    "-I", (Join-Path $Root "kernel\core\include"),
    "-I", (Join-Path $Root "kernel\arch\amd64\include"),
    "-I", (Join-Path $Root "tests"),
    "-m64", "-mcmodel=large", "-mno-red-zone", "-mno-mmx", "-mno-sse",
    "-mno-sse2", "-fno-pic", "-fno-pie", "-fno-PIC", "-fno-PIE",
    "-mno-stack-arg-probe", "-fno-stack-check", "-fno-stack-protector",
    "-D__x86_64__", "-fstack-usage", "-MMD", "-MP", "-c"
)

$units = @(
    @{ Name = "lexer"; Source = "navigator_javascript/lexer.cpp" },
    @{ Name = "ast"; Source = "navigator_javascript/ast.cpp" },
    @{ Name = "portable-frontend-test"; Source = "tests/navigator_javascript_portable_lexer_test.cpp" }
)
foreach ($unit in $units) {
    $object = Join-Path $OutputDirectory ($unit.Name + ".o")
    $log = Join-Path $OutputDirectory ("guest-" + $unit.Name + "-compile.log")
    $output = & $targetCompilerPath @targetFlags $unit.Source "-o" $object 2>&1
    $exitCode = $LASTEXITCODE
    $output | Set-Content -LiteralPath $log -Encoding UTF8
    if ($exitCode -ne 0) { throw "Guest-target $($unit.Name) compile failed; see $log" }
}

$nm = Join-Path (Split-Path -Parent $targetCompilerPath) "nm.exe"
if (-not (Test-Path -LiteralPath $nm)) {
    $nmCommand = Get-Command "x86_64-w64-mingw32-nm" -ErrorAction Stop
    $nm = $nmCommand.Source
}
foreach ($unit in $units) {
    $object = Join-Path $OutputDirectory ($unit.Name + ".o")
    $log = Join-Path $OutputDirectory ("guest-" + $unit.Name + "-undefined-symbols.log")
    $output = & $nm -u $object 2>&1
    $exitCode = $LASTEXITCODE
    $output | Set-Content -LiteralPath $log -Encoding UTF8
    if ($exitCode -ne 0) { throw "Could not inspect guest-target object $object" }
    if ($unit.Name -ne "portable-frontend-test" -and $output.Count -ne 0) {
        throw "Guest-target object $object has undefined symbols; see $log"
    }
}

Write-Output "Portable frontend shared lexer and AST checks PASS."
Write-Output "Guest objects compiled for $targetTriple with exceptions and RTTI disabled."
Write-Output "Lexer and AST implementation objects have no undefined symbols."
Write-Output "The qualification-test object has expected references to those objects and the C++ entry shim."
Write-Output "Guest objects were inspected only; no guest execution or application link was attempted."
