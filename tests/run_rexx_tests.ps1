$ErrorActionPreference = "Stop"

$rootDir = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$gccCommand = Get-Command gcc -ErrorAction SilentlyContinue
if ($null -eq $gccCommand) {
    throw "gcc is required; this checkout documents a C11 cc/gcc hosted build."
}

$buildDirectory = Join-Path $rootDir ("tmp\rexx-rx1-tests-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $buildDirectory | Out-Null
$runtimeTest = Join-Path $buildDirectory "rexx_runtime_tests.exe"
$cliTest = Join-Path $buildDirectory "rexx_cli.exe"
$commonSources = @(
    (Join-Path $rootDir "rexx_lexer.c"),
    (Join-Path $rootDir "rexx_parser.c"),
    (Join-Path $rootDir "rexx_compiler.c"),
    (Join-Path $rootDir "rexx_vm.c"),
    (Join-Path $rootDir "rexx_builtin.c")
)

Write-Host "Building hosted REXX runtime tests with $($gccCommand.Source)"
& $gccCommand.Source -std=c11 -Wall -Wextra -Wpedantic -Werror `
    (Join-Path $rootDir "rexx_main.c") @commonSources -lm -o $runtimeTest
if ($LASTEXITCODE -ne 0) {
    throw "Hosted runtime test build failed with exit code $LASTEXITCODE."
}

Write-Host "Building hosted REXX CLI test driver"
& $gccCommand.Source -std=c11 -Wall -Wextra -Wpedantic -Werror `
    (Join-Path $PSScriptRoot "rexx_cli_test_driver.c") `
    (Join-Path $rootDir "rexx_cli.c") (Join-Path $rootDir "rexx_os_io.c") `
    @commonSources -lm -o $cliTest
if ($LASTEXITCODE -ne 0) {
    throw "Hosted CLI test build failed with exit code $LASTEXITCODE."
}

Write-Host "Running hosted runtime assertions"
& $runtimeTest
if ($LASTEXITCODE -ne 0) {
    throw "Hosted runtime assertions failed with exit code $LASTEXITCODE."
}

$script:cliPassed = 0
$script:reginaPassed = 0
$script:reginaCommand = Get-Command regina -ErrorAction SilentlyContinue
function Assert-CliCase {
    param(
        [string]$Name,
        [string]$Source,
        [string[]]$ExpectedOutput,
        [int]$ExpectedExit,
        [string]$ExpectedDiagnostic = "",
        [switch]$CompareRegina
    )

    $sourcePath = Join-Path $buildDirectory ($Name + ".rex")
    [System.IO.File]::WriteAllText(
        $sourcePath,
        $Source,
        [System.Text.UTF8Encoding]::new($false))

    $actualOutput = @(& $cliTest $sourcePath 2>&1 | ForEach-Object { $_.ToString() })
    $actualExit = $LASTEXITCODE
    if ($actualExit -ne $ExpectedExit) {
        throw "$Name returned $actualExit; expected $ExpectedExit. Output: $($actualOutput -join ' | ')"
    }

    if ($ExpectedDiagnostic.Length -ne 0) {
        $actualText = [string]::Join("`n", $actualOutput)
        if (-not $actualText.Contains($ExpectedDiagnostic)) {
            throw "$Name did not report '$ExpectedDiagnostic'. Output: $actualText"
        }
    } else {
        $actualText = [string]::Join("`n", $actualOutput)
        $expectedText = [string]::Join("`n", $ExpectedOutput)
        if ($actualText -cne $expectedText) {
            throw "$Name output mismatch. Expected '$expectedText'; got '$actualText'."
        }
    }

    if ($CompareRegina -and $null -ne $script:reginaCommand) {
        $referenceOutput = @(& $script:reginaCommand.Source $sourcePath 2>&1 |
            ForEach-Object { $_.ToString() })
        $referenceExit = $LASTEXITCODE
        $referenceText = [string]::Join("`n", $referenceOutput)
        $expectedText = [string]::Join("`n", $ExpectedOutput)
        if ($referenceExit -ne $ExpectedExit -or $referenceText -cne $expectedText) {
            throw "$Name differed from Regina. Expected '$expectedText'/$ExpectedExit; got '$referenceText'/$referenceExit."
        }
        ++$script:reginaPassed
        Write-Host "[PASS] Regina differential $Name"
    }

    ++$script:cliPassed
    Write-Host "[PASS] CLI $Name"
}

$assignmentSource = @'
MiXeD = 42
say mixed
say MIXED
'@
Assert-CliCase "assignment_case" $assignmentSource @("42", "42") 0 -CompareRegina

$arithmeticSource = @'
answer = 5 + 3 * 2
say answer
'@
Assert-CliCase "arithmetic" $arithmeticSource @("11") 0 -CompareRegina

$concatenationSource = @'
first = "guide" "XOS"
second = first || "!"
say second
'@
Assert-CliCase "string_concatenation" $concatenationSource @("guide XOS!") 0 -CompareRegina

$comparisonSource = @'
value = 8
if value > 5 then say "Greater" else say "Less"
'@
Assert-CliCase "comparison_conditional" $comparisonSource @("Greater") 0 -CompareRegina

$stemSource = @'
server.1 = "active"
say SERVER.1
'@
Assert-CliCase "stem_variable" $stemSource @("active") 0 -CompareRegina

$builtinSource = @'
say length("REXX")
say substr("guideXOS", 6, 3)
say word("one two three", 2)
say words("one two three")
say strip("  trim  ")
say pos("two", "one two")
say translate("abc")
'@
Assert-CliCase "builtin_string_functions" $builtinSource @("4", "XOS", "two", "3", "trim", "5", "ABC") 0 -CompareRegina

$saySource = @'
say "done"
'@
Assert-CliCase "say_and_normal_completion" $saySource @("done") 0 -CompareRegina

$addressSource = @'
address KERNEL "host command"
say RC
'@
Assert-CliCase "address_rc_is_not_process_status" $addressSource @("127") 0

$exitZeroSource = @'
say "before"
exit 0
say "after"
'@
Assert-CliCase "exit_zero" $exitZeroSource @("before") 0 -CompareRegina

$exitNonzeroSource = @'
say "before"
exit 7
say "after"
'@
Assert-CliCase "exit_nonzero" $exitNonzeroSource @("before") 7 -CompareRegina

$exitStringSource = @'
exit "7"
'@
Assert-CliCase "exit_numeric_string" $exitStringSource @() 7 -CompareRegina

$exitNoValueSource = @'
exit
'@
Assert-CliCase "exit_without_value" $exitNoValueSource @() 0 -CompareRegina

$exitFractionSource = @'
exit 7.5
'@
Assert-CliCase "exit_noninteger_status" $exitFractionSource @() 1 `
    "EXIT value is not an integer process status"

$syntaxErrorSource = @'
if then say "bad"
'@
Assert-CliCase "syntax_error" $syntaxErrorSource @() 1 "syntax error"

$unsupportedSource = @'
say 8 / 2
'@
Assert-CliCase "unsupported_runtime_opcode" $unsupportedSource @() 1 "opcode is not implemented by this VM"

$oversizePath = Join-Path $buildDirectory "source_limit.rex"
$oversizeStream = [System.IO.File]::Open(
    $oversizePath,
    [System.IO.FileMode]::CreateNew,
    [System.IO.FileAccess]::Write,
    [System.IO.FileShare]::None)
try {
    $oversizeStream.SetLength(16777217)
} finally {
    $oversizeStream.Dispose()
}
$limitOutput = @(& $cliTest $oversizePath 2>&1 | ForEach-Object { $_.ToString() })
$limitExit = $LASTEXITCODE
if ($limitExit -ne 1 -or
    -not ([string]::Join("`n", $limitOutput)).Contains("file exceeds the 16 MiB limit")) {
    throw "Source-size limit did not fail safely. Exit=$limitExit Output=$($limitOutput -join ' | ')"
}
$script:cliPassed++
Write-Host "[PASS] CLI source-size memory limit"

$regina = $script:reginaCommand
if ($null -eq $regina) {
    Write-Host "Regina REXX is unavailable; expected compatibility outputs are documented in the RX1 handoff."
} else {
    Write-Host "Regina differential cases passed: $script:reginaPassed/$script:reginaPassed"
}

Write-Host "Hosted CLI cases passed: $script:cliPassed/$script:cliPassed"
Write-Host "Build and temporary script artifacts: $buildDirectory"
