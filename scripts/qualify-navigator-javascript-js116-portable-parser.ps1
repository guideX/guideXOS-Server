param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot),
    [string]$OutputDirectory = 'E:\CodexArtifacts\guideXOSServer_NAVIGATOR_JS_SUPPORT\JS116\run-20261111-01',
    [string]$TargetCompiler = 'x86_64-w64-mingw32-g++'
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $Root
New-Item -ItemType Directory -Force -Path (Join-Path $OutputDirectory 'hosted') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $OutputDirectory 'target') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $OutputDirectory 'logs') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $OutputDirectory 'metadata') | Out-Null

$targetCommand = Get-Command $TargetCompiler -ErrorAction Stop
$targetCompilerPath = $targetCommand.Source
$targetVersion = (& $targetCompilerPath -dumpversion).Trim()
$targetTriple = (& $targetCompilerPath -dumpmachine).Trim()
$toolchainRoot = Split-Path -Parent (Split-Path -Parent $targetCompilerPath)
$cxxInclude = Join-Path $toolchainRoot "include\c++\$targetVersion"
$targetCxxInclude = Join-Path $cxxInclude $targetTriple
if (-not (Test-Path -LiteralPath (Join-Path $cxxInclude 'cstddef')) -or
    -not (Test-Path -LiteralPath $targetCxxInclude)) {
    throw "Could not locate target C++ headers for $targetCompilerPath."
}

$hostCompiler = (Get-Command 'g++' -ErrorAction Stop).Source
$hostTests = @(
    @{
        Name = 'portable-lexer-ast'
        Output = 'portable_lexer_ast_test.exe'
        Arguments = @(
            '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
            '-DGXOS_NAVIGATOR_FRONTEND_FREESTANDING', '-I', $Root,
            'tests/navigator_javascript_portable_lexer_test.cpp',
            'navigator_javascript/lexer.cpp', 'navigator_javascript/ast.cpp'
        )
    },
    @{
        Name = 'portable-shared-parser'
        Output = 'portable_parser_test.exe'
        Arguments = @(
            '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
            '-I', $Root, 'tests/navigator_javascript_portable_parser_test.cpp',
            'navigator_javascript/lexer.cpp', 'navigator_javascript/ast.cpp',
            'navigator_javascript/parser.cpp'
        )
    }
)

$hostResults = @()
foreach ($test in $hostTests) {
    $executable = Join-Path $OutputDirectory "hosted\$($test.Output)"
    $compileLog = Join-Path $OutputDirectory "logs\$($test.Name)-host-build.log"
    $runLog = Join-Path $OutputDirectory "logs\$($test.Name)-host-run.log"
    $hostArguments = $test.Arguments + @('-o', $executable)
    $compilerOutput = & $hostCompiler @hostArguments 2>&1
    $compileExit = $LASTEXITCODE
    $compilerOutput | Set-Content -LiteralPath $compileLog -Encoding UTF8
    if ($compileExit -ne 0) { throw "Hosted $($test.Name) compile failed; see $compileLog" }
    $testOutput = & $executable 2>&1
    $testExit = $LASTEXITCODE
    $testOutput | Set-Content -LiteralPath $runLog -Encoding UTF8
    if ($testExit -ne 0) { throw "Hosted $($test.Name) checks failed ($testExit); see $runLog" }
    $hostResults += [ordered]@{
        name = $test.Name
        compileExitCode = $compileExit
        runExitCode = $testExit
        executable = $executable
        compileLog = $compileLog
        runLog = $runLog
    }
}

$targetFlags = @(
    '-std=c++17', '-ffreestanding', '-O2', '-Wall', '-Wextra', '-Werror',
    '-pedantic', '-fno-exceptions', '-fno-rtti', '-nostdlib', '-nostdinc++',
    '-fno-builtin', '-DGXOS_BARE_METAL',
    '-DGXOS_NAVIGATOR_FRONTEND_FREESTANDING',
    '-isystem', $cxxInclude, '-isystem', $targetCxxInclude,
    '-I', (Join-Path $Root 'kernel'), '-I', $Root,
    '-I', (Join-Path $Root 'kernel\core\include'),
    '-I', (Join-Path $Root 'kernel\arch\amd64\include'),
    '-I', (Join-Path $Root 'tests'),
    '-m64', '-mcmodel=large', '-mno-red-zone', '-mno-mmx', '-mno-sse',
    '-mno-sse2', '-fno-pic', '-fno-pie', '-fno-PIC', '-fno-PIE',
    '-mno-stack-arg-probe', '-fno-stack-check', '-fno-stack-protector',
    '-D__x86_64__', '-fstack-usage', '-MMD', '-MP', '-c'
)

$units = @(
    @{ Name = 'lexer'; Source = 'navigator_javascript/lexer.cpp'; Implementation = $true },
    @{ Name = 'ast'; Source = 'navigator_javascript/ast.cpp'; Implementation = $true },
    @{ Name = 'parser'; Source = 'navigator_javascript/parser.cpp'; Implementation = $true },
    @{ Name = 'portable-lexer-ast-test'; Source = 'tests/navigator_javascript_portable_lexer_test.cpp'; Implementation = $false },
    @{ Name = 'portable-parser-test'; Source = 'tests/navigator_javascript_portable_parser_test.cpp'; Implementation = $false }
)

$targetResults = @()
foreach ($unit in $units) {
    $object = Join-Path $OutputDirectory "target\$($unit.Name).o"
    $compileLog = Join-Path $OutputDirectory "logs\$($unit.Name)-target-compile.log"
    $compilerOutput = & $targetCompilerPath @targetFlags $unit.Source '-o' $object 2>&1
    $compileExit = $LASTEXITCODE
    $compilerOutput | Set-Content -LiteralPath $compileLog -Encoding UTF8
    if ($compileExit -ne 0) { throw "Freestanding $($unit.Name) compile failed; see $compileLog" }
    $targetResults += [ordered]@{
        name = $unit.Name
        source = $unit.Source
        object = $object
        compileExitCode = $compileExit
        compileLog = $compileLog
        stackUsage = [System.IO.Path]::ChangeExtension($object, '.su')
    }
}

$binDirectory = Split-Path -Parent $targetCompilerPath
$nm = Join-Path $binDirectory 'x86_64-w64-mingw32-nm.exe'
$objdump = Join-Path $binDirectory 'x86_64-w64-mingw32-objdump.exe'
$linker = Join-Path $binDirectory 'x86_64-w64-mingw32-ld.exe'
$archiver = Join-Path $binDirectory 'x86_64-w64-mingw32-ar.exe'
if (-not (Test-Path -LiteralPath $nm)) { $nm = Join-Path $binDirectory 'nm.exe' }
if (-not (Test-Path -LiteralPath $objdump)) { $objdump = Join-Path $binDirectory 'objdump.exe' }
if (-not (Test-Path -LiteralPath $linker)) { $linker = Join-Path $binDirectory 'ld.exe' }
if (-not (Test-Path -LiteralPath $archiver)) { $archiver = Join-Path $binDirectory 'ar.exe' }
foreach ($tool in @($nm, $objdump, $linker, $archiver)) {
    if (-not (Test-Path -LiteralPath $tool)) { throw "Required target audit tool is missing: $tool" }
}

$auditResults = @()
foreach ($unit in $units) {
    $object = Join-Path $OutputDirectory "target\$($unit.Name).o"
    $undefinedLog = Join-Path $OutputDirectory "logs\$($unit.Name)-undefined-symbols.log"
    $symbolOutput = & $nm '-u' '-C' $object 2>&1
    $symbolExit = $LASTEXITCODE
    $symbolOutput | Set-Content -LiteralPath $undefinedLog -Encoding UTF8
    if ($symbolExit -ne 0) { throw "nm failed for $object" }
    if ($unit.Implementation -and $unit.Name -ne 'parser' -and
        $symbolOutput.Count -ne 0) {
        throw "Implementation object $object has undefined symbols; see $undefinedLog"
    }
    if ($unit.Name -eq 'parser') {
        $unexpected = @($symbolOutput | Where-Object {
            $_ -notmatch '^\s+U gxos::javascript::Ast::'
        })
        if ($unexpected.Count -ne 0) {
            throw "Parser object references a non-AST dependency; see $undefinedLog"
        }
    }
    $formatLog = Join-Path $OutputDirectory "logs\$($unit.Name)-object-format.log"
    $formatOutput = & $objdump '-f' $object 2>&1
    $formatExit = $LASTEXITCODE
    $formatOutput | Set-Content -LiteralPath $formatLog -Encoding UTF8
    if ($formatExit -ne 0) { throw "objdump failed for $object" }
    $auditResults += [ordered]@{
        name = $unit.Name
        undefinedSymbols = @($symbolOutput)
        undefinedSymbolLog = $undefinedLog
        objectFormat = @($formatOutput)
        objectFormatLog = $formatLog
    }
}

$implementationObjects = @(
    (Join-Path $OutputDirectory 'target\lexer.o'),
    (Join-Path $OutputDirectory 'target\ast.o'),
    (Join-Path $OutputDirectory 'target\parser.o')
)
$relocatable = Join-Path $OutputDirectory 'target\frontend-component-coff.o'
$linkLog = Join-Path $OutputDirectory 'logs\frontend-relocatable-link.log'
$linkOutput = & $linker '-r' @implementationObjects '-o' $relocatable 2>&1
$linkExit = $LASTEXITCODE
$linkOutput | Set-Content -LiteralPath $linkLog -Encoding UTF8
$relocatableUndefinedLog = Join-Path $OutputDirectory 'logs\frontend-relocatable-undefined-symbols.log'
$relocatableUndefined = @()
if ($linkExit -eq 0) {
    $relocatableUndefined = @(& $nm '-u' '-C' $relocatable 2>&1)
    $nmExit = $LASTEXITCODE
    $relocatableUndefined | Set-Content -LiteralPath $relocatableUndefinedLog -Encoding UTF8
    if ($nmExit -ne 0) { throw 'nm failed for dependency-closed relocatable frontend.' }
    if ($relocatableUndefined.Count -ne 0) {
        throw "Relocatable frontend has undefined symbols; see $relocatableUndefinedLog"
    }
}

$archive = Join-Path $OutputDirectory 'target\libnavigator-frontend.a'
$archiveLog = Join-Path $OutputDirectory 'logs\frontend-archive.log'
$archiveOutput = & $archiver 'rcs' $archive @implementationObjects 2>&1
$archiveExit = $LASTEXITCODE
$archiveOutput | Set-Content -LiteralPath $archiveLog -Encoding UTF8
if ($archiveExit -ne 0) { throw "Could not create portable frontend archive; see $archiveLog" }

$linkFormat = @()
if ($linkExit -eq 0) {
    $linkFormat = @(& $objdump '-f' $relocatable 2>&1)
    $linkFormat | Set-Content -LiteralPath (Join-Path $OutputDirectory 'logs\frontend-relocatable-format.log') -Encoding UTF8
}

$summary = [ordered]@{
    targetCompiler = $targetCompilerPath
    targetVersion = $targetVersion
    targetTriple = $targetTriple
    targetFlags = $targetFlags
    cxxInclude = $cxxInclude
    targetCxxInclude = $targetCxxInclude
    hostCompiler = $hostCompiler
    hostedResults = $hostResults
    targetResults = $targetResults
    audits = $auditResults
    relocatableLinkExitCode = $linkExit
    relocatableObject = $relocatable
    relocatableUndefinedSymbols = $relocatableUndefined
    relocatableFormat = $linkFormat
    archiveExitCode = $archiveExit
    archive = $archive
    completedUtc = [DateTime]::UtcNow.ToString('o')
}
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'metadata\portable-parser-qualification.json') -Encoding UTF8
Write-Output "Hosted lexer/AST/parser qualification passed."
Write-Output "Shared lexer, AST, and parser compiled for $targetTriple with exceptions, RTTI, implicit C++ include discovery, and runtime libraries disabled."
Write-Output "Freestanding implementation object undefined-symbol audits passed."
Write-Output "Relocatable-link exit code: $linkExit; archive exit code: $archiveExit."
