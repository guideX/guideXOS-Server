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
$rx2Test = Join-Path $buildDirectory "rexx_rx2_tests.exe"
$rx2NumericTest = Join-Path $buildDirectory "rexx_rx2_native_numeric_tests.exe"
$rx3DecimalTest = Join-Path $buildDirectory "rexx_rx3_decimal_tests.exe"
$rx3AllocFailureTest = Join-Path $buildDirectory "rexx_rx3_alloc_failure_tests.exe"
$rx4ArithmeticTest = Join-Path $buildDirectory "rexx_rx4_arithmetic_tests.exe"
$rx5NumericContextTest = Join-Path $buildDirectory "rexx_rx5_numeric_context_tests.exe"
$rx6AllocFailureTest = Join-Path $buildDirectory "rexx_rx6_parse_alloc_failure_tests.exe"
$rx7RoutinesTest = Join-Path $buildDirectory "rexx_rx7_routines_tests.exe"
$rx7AllocFailureTest = Join-Path $buildDirectory "rexx_rx7_alloc_failure_tests.exe"
$rx8Test = Join-Path $buildDirectory "rexx_rx8_semantics_alloc_tests.exe"
$rx9Test = Join-Path $buildDirectory "rexx_rx9_condition_tests.exe"
$rx10Test = Join-Path $buildDirectory "rexx_rx10_interpret_tests.exe"
$rx12Test = Join-Path $buildDirectory "rexx_rx12_builtins_tests.exe"
$rx2ShellBridgeTest = Join-Path $buildDirectory "rexx_rx2_shell_bridge_tests.exe"
$commonSources = @(
    (Join-Path $rootDir "rexx_lexer.c"),
    (Join-Path $rootDir "rexx_parser.c"),
    (Join-Path $rootDir "rexx_compiler.c"),
    (Join-Path $rootDir "rexx_vm.c"),
    (Join-Path $rootDir "rexx_builtin.c"),
    (Join-Path $rootDir "rexx_numeric.c")
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
    (Join-Path $rootDir "rexx_output.c") `
    @commonSources -lm -o $cliTest
if ($LASTEXITCODE -ne 0) {
    throw "Hosted CLI test build failed with exit code $LASTEXITCODE."
}

Write-Host "Building RX2 VFS and output integration tests"
& $gccCommand.Source -std=c11 -Wall -Wextra -Wpedantic -Werror `
    (Join-Path $PSScriptRoot "rexx_rx2_test_driver.c") `
    (Join-Path $rootDir "rexx_cli.c") (Join-Path $rootDir "rexx_os_io.c") `
    (Join-Path $rootDir "rexx_output.c") `
    @commonSources -lm -o $rx2Test
if ($LASTEXITCODE -ne 0) {
    throw "RX2 VFS/output integration test build failed with exit code $LASTEXITCODE."
}

Write-Host "Building RX2 freestanding numeric adapter tests"
& $gccCommand.Source -std=c11 -DGXOS_BARE_METAL -ffreestanding `
    -Wall -Wextra -Wpedantic -Werror `
    (Join-Path $PSScriptRoot "rexx_rx2_native_numeric_test.c") `
    (Join-Path $rootDir "rexx_numeric.c") -o $rx2NumericTest
if ($LASTEXITCODE -ne 0) {
    throw "RX2 freestanding numeric test build failed with exit code $LASTEXITCODE."
}

Write-Host "Building RX3 decimal arithmetic assertions"
& $gccCommand.Source -std=c11 -Wall -Wextra -Wpedantic -Werror `
    (Join-Path $PSScriptRoot "rexx_rx3_decimal_test.c") `
    (Join-Path $rootDir "rexx_numeric.c") -lm -o $rx3DecimalTest
if ($LASTEXITCODE -ne 0) {
    throw "RX3 decimal test build failed with exit code $LASTEXITCODE."
}

Write-Host "Building RX3 hosted allocation-failure qualification tests"
& $gccCommand.Source -std=c11 -DREXX_VM_TEST_ALLOCATOR `
    -Wall -Wextra -Wpedantic -Werror `
    (Join-Path $PSScriptRoot "rexx_rx3_alloc_failure_test.c") `
    @commonSources -lm -o $rx3AllocFailureTest
if ($LASTEXITCODE -ne 0) {
    throw "RX3 allocation-failure test build failed with exit code $LASTEXITCODE."
}

Write-Host "Building RX4 decimal arithmetic tests"
& $gccCommand.Source -std=c11 -Wall -Wextra -Wpedantic -Werror `
    (Join-Path $PSScriptRoot "rexx_rx4_arithmetic_test.c") `
    @commonSources -lm -o $rx4ArithmeticTest
if ($LASTEXITCODE -ne 0) {
    throw "RX4 arithmetic test build failed with exit code $LASTEXITCODE."
}

Write-Host "Building RX5 numeric-context tests"
& $gccCommand.Source -std=c11 -Wall -Wextra -Wpedantic -Werror `
    (Join-Path $PSScriptRoot "rexx_rx5_numeric_context_test.c") `
    @commonSources -lm -o $rx5NumericContextTest
if ($LASTEXITCODE -ne 0) {
    throw "RX5 numeric-context test build failed with exit code $LASTEXITCODE."
}

Write-Host "Building RX6 PARSE allocation-failure tests"
& $gccCommand.Source -std=c11 -DREXX_VM_TEST_ALLOCATOR `
    -DREXX_PARSER_TEST_ALLOCATOR -DREXX_COMPILER_TEST_ALLOCATOR `
    -Wall -Wextra -Wpedantic -Werror `
    (Join-Path $PSScriptRoot "rexx_rx6_parse_alloc_failure_test.c") `
    @commonSources -lm -o $rx6AllocFailureTest
if ($LASTEXITCODE -ne 0) {
    throw "RX6 PARSE allocation-failure test build failed with exit code $LASTEXITCODE."
}

Write-Host "Building RX7 internal routine C-level tests"
& $gccCommand.Source -std=c11 -Wall -Wextra -Wpedantic -Werror `
    (Join-Path $PSScriptRoot "rexx_rx7_routines_test.c") `
    @commonSources -lm -o $rx7RoutinesTest
if ($LASTEXITCODE -ne 0) {
    throw "RX7 routine test build failed with exit code $LASTEXITCODE."
}

Write-Host "Building RX7 parser/compiler/VM allocation-failure tests"
& $gccCommand.Source -std=c11 -DREXX_VM_TEST_ALLOCATOR `
    -DREXX_PARSER_TEST_ALLOCATOR -DREXX_COMPILER_TEST_ALLOCATOR `
    -Wall -Wextra -Wpedantic -Werror `
    (Join-Path $PSScriptRoot "rexx_rx7_alloc_failure_test.c") `
    @commonSources -lm -o $rx7AllocFailureTest
if ($LASTEXITCODE -ne 0) {
    throw "RX7 allocation-failure test build failed with exit code $LASTEXITCODE."
}

Write-Host "Building RX8 SIGNAL, subsidiary EXPOSE, and allocation tests"
& $gccCommand.Source -std=c11 -DREXX_VM_TEST_ALLOCATOR `
    -DREXX_PARSER_TEST_ALLOCATOR -DREXX_COMPILER_TEST_ALLOCATOR `
    -Wall -Wextra -Wpedantic -Werror `
    (Join-Path $PSScriptRoot "rexx_rx8_semantics_alloc_test.c") `
    @commonSources -lm -o $rx8Test
if ($LASTEXITCODE -ne 0) {
    throw "RX8 semantic and allocation test build failed with exit code $LASTEXITCODE."
}

Write-Host "Building RX9 condition-trap and allocation tests"
& $gccCommand.Source -std=c11 -DREXX_VM_TEST_ALLOCATOR `
    -DREXX_PARSER_TEST_ALLOCATOR -DREXX_COMPILER_TEST_ALLOCATOR `
    -Wall -Wextra -Wpedantic -Werror `
    (Join-Path $PSScriptRoot "rexx_rx9_condition_test.c") `
    @commonSources -lm -o $rx9Test
if ($LASTEXITCODE -ne 0) {
    throw "RX9 condition test build failed with exit code $LASTEXITCODE."
}

Write-Host "Building RX10 INTERPRET and allocation tests"
& $gccCommand.Source -std=c11 -DREXX_VM_TEST_ALLOCATOR `
    -DREXX_PARSER_TEST_ALLOCATOR -DREXX_COMPILER_TEST_ALLOCATOR `
    -Wall -Wextra -Wpedantic -Werror `
    (Join-Path $PSScriptRoot "rexx_rx10_interpret_test.c") `
    @commonSources -lm -o $rx10Test
if ($LASTEXITCODE -ne 0) {
    throw "RX10 INTERPRET test build failed with exit code $LASTEXITCODE."
}

Write-Host "Building RX12 classic string and word built-in assertions"
& $gccCommand.Source -std=c11 -DREXX_VM_TEST_ALLOCATOR `
    -Wall -Wextra -Wpedantic -Werror `
    (Join-Path $PSScriptRoot "rexx_rx12_builtins_test.c") `
    @commonSources -lm -o $rx12Test
if ($LASTEXITCODE -ne 0) {
    throw "RX12 built-in test build failed with exit code $LASTEXITCODE."
}

$gxxCommand = Get-Command g++ -ErrorAction SilentlyContinue
if ($null -eq $gxxCommand) {
    throw "g++ is required for the RX2 shell bridge integration test."
}
Write-Host "Building RX2 C++ shell bridge integration tests"
& $gxxCommand.Source -std=c++14 -Wall -Wextra -Wpedantic -Werror `
    (Join-Path $PSScriptRoot "rexx_rx2_shell_bridge_test.cpp") `
    (Join-Path $rootDir "kernel\shell\rexx_shell_cmd.cpp") `
    ("-I" + (Join-Path $rootDir "kernel\core\include")) `
    -o $rx2ShellBridgeTest
if ($LASTEXITCODE -ne 0) {
    throw "RX2 shell bridge integration test build failed with exit code $LASTEXITCODE."
}

Write-Host "Running hosted runtime assertions"
& $runtimeTest
if ($LASTEXITCODE -ne 0) {
    throw "Hosted runtime assertions failed with exit code $LASTEXITCODE."
}

Write-Host "Running RX2 VFS and output assertions"
& $rx2Test
if ($LASTEXITCODE -ne 0) {
    throw "RX2 VFS/output assertions failed with exit code $LASTEXITCODE."
}

Write-Host "Running RX2 freestanding numeric assertions"
& $rx2NumericTest
if ($LASTEXITCODE -ne 0) {
    throw "RX2 freestanding numeric assertions failed with exit code $LASTEXITCODE."
}

Write-Host "Running RX3 decimal arithmetic assertions"
& $rx3DecimalTest
if ($LASTEXITCODE -ne 0) {
    throw "RX3 decimal assertions failed with exit code $LASTEXITCODE."
}

Write-Host "Running RX3 allocation-failure assertions"
& $rx3AllocFailureTest
if ($LASTEXITCODE -ne 0) {
    throw "RX3 allocation-failure assertions failed with exit code $LASTEXITCODE."
}

Write-Host "Running RX4 decimal arithmetic assertions"
& $rx4ArithmeticTest
if ($LASTEXITCODE -ne 0) {
    throw "RX4 arithmetic assertions failed with exit code $LASTEXITCODE."
}

Write-Host "Running RX5 numeric-context assertions"
& $rx5NumericContextTest
if ($LASTEXITCODE -ne 0) {
    throw "RX5 numeric-context assertions failed with exit code $LASTEXITCODE."
}

Write-Host "Running RX6 PARSE allocation-failure assertions"
& $rx6AllocFailureTest
if ($LASTEXITCODE -ne 0) {
    throw "RX6 PARSE allocation-failure assertions failed with exit code $LASTEXITCODE."
}

Write-Host "Running RX7 internal routine assertions"
& $rx7RoutinesTest
if ($LASTEXITCODE -ne 0) {
    throw "RX7 routine assertions failed with exit code $LASTEXITCODE."
}

Write-Host "Running RX7 allocation-failure assertions"
& $rx7AllocFailureTest
if ($LASTEXITCODE -ne 0) {
    throw "RX7 allocation-failure assertions failed with exit code $LASTEXITCODE."
}

Write-Host "Running RX8 semantic, allocation-failure, and recovery assertions"
& $rx8Test
if ($LASTEXITCODE -ne 0) {
    throw "RX8 assertions failed with exit code $LASTEXITCODE."
}

Write-Host "Running RX9 condition-trap and allocation assertions"
& $rx9Test
if ($LASTEXITCODE -ne 0) {
    throw "RX9 condition assertions failed with exit code $LASTEXITCODE."
}

Write-Host "Running RX10 INTERPRET and allocation assertions"
& $rx10Test
if ($LASTEXITCODE -ne 0) {
    throw "RX10 INTERPRET assertions failed with exit code $LASTEXITCODE."
}

Write-Host "Running RX12 classic string and word built-in assertions"
& $rx12Test
if ($LASTEXITCODE -ne 0) {
    throw "RX12 built-in assertions failed with exit code $LASTEXITCODE."
}

Write-Host "Running RX2 shell bridge assertions"
& $rx2ShellBridgeTest
if ($LASTEXITCODE -ne 0) {
    throw "RX2 shell bridge assertions failed with exit code $LASTEXITCODE."
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
        [switch]$CompareRegina,
        [string[]]$ScriptArguments = @()
    )

    $sourcePath = Join-Path $buildDirectory ($Name + ".rex")
    [System.IO.File]::WriteAllText(
        $sourcePath,
        $Source,
        [System.Text.UTF8Encoding]::new($false))

    $actual = Invoke-CapturedProcess $cliTest (@($sourcePath) + $ScriptArguments)
    $actualExit = $actual.ExitCode
    $actualText = (($actual.Stdout + $actual.Stderr) -replace "`r`n", "`n")
    $actualText = $actualText.TrimEnd([char[]]"`n")
    if ($actualExit -ne $ExpectedExit) {
        throw "$Name returned $actualExit; expected $ExpectedExit. Output: $actualText"
    }

    if ($ExpectedDiagnostic.Length -ne 0) {
        if (-not $actualText.Contains($ExpectedDiagnostic)) {
            throw "$Name did not report '$ExpectedDiagnostic'. Output: $actualText"
        }
    } else {
        $expectedText = [string]::Join("`n", $ExpectedOutput)
        if ($actualText -cne $expectedText) {
            throw "$Name output mismatch. Expected '$expectedText'; got '$actualText'."
        }
    }

    if ($CompareRegina -and $null -ne $script:reginaCommand) {
        $reference = Invoke-CapturedProcess $script:reginaCommand.Source @($sourcePath)
        $referenceText = (($reference.Stdout + $reference.Stderr) -replace "`r`n", "`n")
        $referenceText = $referenceText.TrimEnd([char[]]"`n")
        $expectedText = [string]::Join("`n", $ExpectedOutput)
        if ($reference.ExitCode -ne $ExpectedExit -or $referenceText -cne $expectedText) {
            throw "$Name differed from Regina. Expected '$expectedText'/$ExpectedExit; got '$referenceText'/$($reference.ExitCode)."
        }
        ++$script:reginaPassed
        Write-Host "[PASS] Regina differential $Name"
    }

    ++$script:cliPassed
    Write-Host "[PASS] CLI $Name"
}

function ConvertTo-WindowsProcessArgument {
    param([string]$Argument)

    if ($Argument.Length -gt 0 -and $Argument -notmatch '[\s"]') {
        return $Argument
    }

    $builder = [System.Text.StringBuilder]::new()
    [void]$builder.Append([char]0x22)
    $backslashCount = 0
    foreach ($character in $Argument.ToCharArray()) {
        if ($character -eq [char]0x5c) {
            ++$backslashCount
        } elseif ($character -eq [char]0x22) {
            [void]$builder.Append(('\' * (($backslashCount * 2) + 1)))
            [void]$builder.Append([char]0x22)
            $backslashCount = 0
        } else {
            if ($backslashCount -gt 0) {
                [void]$builder.Append(('\' * $backslashCount))
                $backslashCount = 0
            }
            [void]$builder.Append($character)
        }
    }
    if ($backslashCount -gt 0) {
        [void]$builder.Append(('\' * ($backslashCount * 2)))
    }
    [void]$builder.Append([char]0x22)
    return $builder.ToString()
}

function Invoke-CapturedProcess {
    param(
        [string]$Executable,
        [string[]]$Arguments
    )

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Executable
    if ($null -ne $startInfo.PSObject.Properties['ArgumentList']) {
        foreach ($argument in $Arguments) {
            $startInfo.ArgumentList.Add($argument)
        }
    } else {
        $quotedArguments = foreach ($argument in $Arguments) {
            ConvertTo-WindowsProcessArgument $argument
        }
        $startInfo.Arguments = [string]::Join(' ', [string[]]$quotedArguments)
    }
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = [System.Diagnostics.Process]::Start($startInfo)
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    return [PSCustomObject]@{
        ExitCode = $process.ExitCode
        Stdout = $stdout
        Stderr = $stderr
    }
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

$divisionSource = @'
say 8 / 2
'@
Assert-CliCase "classic_division_opcode" $divisionSource @("4") 0 -CompareRegina

$parseArgSource = @'
parse arg all
say '['||all||']'
'@
Assert-CliCase "parse_arg_case_and_shell_tokenization" $parseArgSource `
    @('[MiXeD  Two Tail"Quoted]') 0 `
    -ScriptArguments @('MiXeD  Two', 'Tail"Quoted')

$parseUpperArgSource = @'
parse upper arg all
say '['||all||']'
'@
Assert-CliCase "parse_upper_arg" $parseUpperArgSource `
    @('[MIXED  TWO TAIL"QUOTED]') 0 `
    -ScriptArguments @('MiXeD  Two', 'Tail"Quoted')

$parseCommaArgSource = @'
parse arg first rest, second
say '['||first||']['||rest||']['||second||']'
'@
Assert-CliCase "parse_arg_comma_templates" $parseCommaArgSource `
    @('[Alpha][Beta Gamma][]') 0 `
    -ScriptArguments @('Alpha Beta', 'Gamma')

$argBuiltinSource = @'
say arg()
say arg(1)
parse arg all
say all
'@
Assert-CliCase "top_level_arg_builtin" $argBuiltinSource `
    @('1', 'alpha beta', 'alpha beta') 0 `
    -ScriptArguments @('alpha', 'beta')

$rx3FixtureDirectory = Join-Path $PSScriptRoot "rexx_rx3"
$script:rx3Passed = 0
$script:rx3ReginaPassed = 0
foreach ($fixture in Get-ChildItem -LiteralPath $rx3FixtureDirectory -Filter "*.rex" | Sort-Object Name) {
    $diagnosticPath = [System.IO.Path]::ChangeExtension($fixture.FullName, ".diag")
    $outputPath = [System.IO.Path]::ChangeExtension($fixture.FullName, ".out")
    $actual = Invoke-CapturedProcess $cliTest @($fixture.FullName)
    $actualText = (($actual.Stdout + $actual.Stderr) -replace "`r`n", "`n").TrimEnd([char[]]"`n")
    if (Test-Path -LiteralPath $diagnosticPath) {
        $expectedDiagnostic = (Get-Content -Raw -LiteralPath $diagnosticPath).Trim()
        if ($actual.ExitCode -ne 1 -or $actual.Stdout.Length -ne 0 -or -not $actualText.Contains($expectedDiagnostic)) {
            throw "RX3 fixture $($fixture.Name) failed its expected diagnostic. Exit=$($actual.ExitCode) Output=$actualText"
        }
    } else {
        if (-not (Test-Path -LiteralPath $outputPath)) {
            throw "RX3 fixture $($fixture.Name) has no .out expectation."
        }
        $expectedOutput = ((Get-Content -Raw -LiteralPath $outputPath) -replace "`r`n", "`n").TrimEnd([char[]]"`n")
        $stdoutText = ($actual.Stdout -replace "`r`n", "`n").TrimEnd([char[]]"`n")
        if ($actual.ExitCode -ne 0 -or $stdoutText -cne $expectedOutput -or $actual.Stderr.Length -ne 0) {
            throw "RX3 fixture $($fixture.Name) output mismatch. Expected '$expectedOutput'; got '$actualText'/$($actual.ExitCode)."
        }
        if ($null -ne $script:reginaCommand) {
            $reference = Invoke-CapturedProcess $script:reginaCommand.Source @($fixture.FullName)
            $referenceText = (($reference.Stdout + $reference.Stderr) -replace "`r`n", "`n").TrimEnd([char[]]"`n")
            if ($reference.ExitCode -ne 0 -or $referenceText -cne $expectedOutput) {
                throw "RX3 fixture $($fixture.Name) expected '$expectedOutput' from Regina; got '$referenceText'/$($reference.ExitCode)."
            }
            ++$script:rx3ReginaPassed
        }
    }
    ++$script:rx3Passed
    Write-Host "[PASS] RX3 fixture $($fixture.Name)"
}

$rx4FixtureDirectory = Join-Path $PSScriptRoot "rexx_rx4"
$script:rx4Passed = 0
$script:rx4ReginaPassed = 0
foreach ($fixture in Get-ChildItem -LiteralPath $rx4FixtureDirectory -Filter "*.rex" | Sort-Object Name) {
    $diagnosticPath = [System.IO.Path]::ChangeExtension($fixture.FullName, ".diag")
    $outputPath = [System.IO.Path]::ChangeExtension($fixture.FullName, ".out")
    $actual = Invoke-CapturedProcess $cliTest @($fixture.FullName)
    $actualText = (($actual.Stdout + $actual.Stderr) -replace "`r`n", "`n").TrimEnd([char[]]"`n")
    if (Test-Path -LiteralPath $diagnosticPath) {
        $expectedDiagnostic = (Get-Content -Raw -LiteralPath $diagnosticPath).Trim()
        if ($actual.ExitCode -ne 1 -or $actual.Stdout.Length -ne 0 -or -not $actualText.Contains($expectedDiagnostic)) {
            throw "RX4 fixture $($fixture.Name) failed its expected diagnostic. Exit=$($actual.ExitCode) Output=$actualText"
        }
    } else {
        if (-not (Test-Path -LiteralPath $outputPath)) {
            throw "RX4 fixture $($fixture.Name) has no .out expectation."
        }
        $expectedOutput = ((Get-Content -Raw -LiteralPath $outputPath) -replace "`r`n", "`n").TrimEnd([char[]]"`n")
        $stdoutText = ($actual.Stdout -replace "`r`n", "`n").TrimEnd([char[]]"`n")
        if ($actual.ExitCode -ne 0 -or $stdoutText -cne $expectedOutput -or $actual.Stderr.Length -ne 0) {
            throw "RX4 fixture $($fixture.Name) output mismatch. Expected '$expectedOutput'; got '$actualText'/$($actual.ExitCode)."
        }
        if ($null -ne $script:reginaCommand) {
            $reference = Invoke-CapturedProcess $script:reginaCommand.Source @($fixture.FullName)
            $referenceText = (($reference.Stdout + $reference.Stderr) -replace "`r`n", "`n").TrimEnd([char[]]"`n")
            if ($reference.ExitCode -ne 0 -or $referenceText -cne $expectedOutput) {
                throw "RX4 fixture $($fixture.Name) expected '$expectedOutput' from Regina; got '$referenceText'/$($reference.ExitCode)."
            }
            ++$script:rx4ReginaPassed
        }
    }
    ++$script:rx4Passed
    Write-Host "[PASS] RX4 fixture $($fixture.Name)"
}

$rx5FixtureDirectory = Join-Path $PSScriptRoot "rexx_rx5"
$script:rx5Passed = 0
$script:rx5ReginaPassed = 0
foreach ($fixture in Get-ChildItem -LiteralPath $rx5FixtureDirectory -Filter "*.rex" | Sort-Object Name) {
    $diagnosticPath = [System.IO.Path]::ChangeExtension($fixture.FullName, ".diag")
    $outputPath = [System.IO.Path]::ChangeExtension($fixture.FullName, ".out")
    $actual = Invoke-CapturedProcess $cliTest @($fixture.FullName)
    $actualText = (($actual.Stdout + $actual.Stderr) -replace "`r`n", "`n").TrimEnd([char[]]"`n")
    if (Test-Path -LiteralPath $diagnosticPath) {
        $expectedDiagnostic = (Get-Content -Raw -LiteralPath $diagnosticPath).Trim()
        if ($actual.ExitCode -ne 1 -or $actual.Stdout.Length -ne 0 -or -not $actualText.Contains($expectedDiagnostic)) {
            throw "RX5 fixture $($fixture.Name) failed its expected diagnostic. Exit=$($actual.ExitCode) Output=$actualText"
        }
    } else {
        if (-not (Test-Path -LiteralPath $outputPath)) {
            throw "RX5 fixture $($fixture.Name) has no .out expectation."
        }
        $expectedOutput = ((Get-Content -Raw -LiteralPath $outputPath) -replace "`r`n", "`n").TrimEnd([char[]]"`n")
        $stdoutText = ($actual.Stdout -replace "`r`n", "`n").TrimEnd([char[]]"`n")
        if ($actual.ExitCode -ne 0 -or $stdoutText -cne $expectedOutput -or $actual.Stderr.Length -ne 0) {
            throw "RX5 fixture $($fixture.Name) output mismatch. Expected '$expectedOutput'; got '$actualText'/$($actual.ExitCode)."
        }
        if ($null -ne $script:reginaCommand) {
            $reference = Invoke-CapturedProcess $script:reginaCommand.Source @($fixture.FullName)
            $referenceText = (($reference.Stdout + $reference.Stderr) -replace "`r`n", "`n").TrimEnd([char[]]"`n")
            if ($reference.ExitCode -ne 0 -or $referenceText -cne $expectedOutput) {
                throw "RX5 fixture $($fixture.Name) expected '$expectedOutput' from Regina; got '$referenceText'/$($reference.ExitCode)."
            }
            ++$script:rx5ReginaPassed
        }
    }
    ++$script:rx5Passed
    Write-Host "[PASS] RX5 fixture $($fixture.Name)"
}

$rx6FixtureDirectory = Join-Path $PSScriptRoot "rexx_rx6"
$script:rx6Passed = 0
$script:rx6ReginaPassed = 0
foreach ($fixture in Get-ChildItem -LiteralPath $rx6FixtureDirectory -Filter "*.rex" | Sort-Object Name) {
    $diagnosticPath = [System.IO.Path]::ChangeExtension($fixture.FullName, ".diag")
    $outputPath = [System.IO.Path]::ChangeExtension($fixture.FullName, ".out")
    $actual = Invoke-CapturedProcess $cliTest @($fixture.FullName)
    $actualText = (($actual.Stdout + $actual.Stderr) -replace "`r`n", "`n").TrimEnd([char[]]"`n")
    if (Test-Path -LiteralPath $diagnosticPath) {
        $expectedDiagnostic = (Get-Content -Raw -LiteralPath $diagnosticPath).Trim()
        if ($actual.ExitCode -ne 1 -or $actual.Stdout.Length -ne 0 -or -not $actualText.Contains($expectedDiagnostic)) {
            throw "RX6 fixture $($fixture.Name) failed its expected diagnostic. Exit=$($actual.ExitCode) Output=$actualText"
        }
    } else {
        if (-not (Test-Path -LiteralPath $outputPath)) {
            throw "RX6 fixture $($fixture.Name) has no .out expectation."
        }
        $expectedOutput = ((Get-Content -Raw -LiteralPath $outputPath) -replace "`r`n", "`n").TrimEnd([char[]]"`n")
        $stdoutText = ($actual.Stdout -replace "`r`n", "`n").TrimEnd([char[]]"`n")
        if ($actual.ExitCode -ne 0 -or $stdoutText -cne $expectedOutput -or $actual.Stderr.Length -ne 0) {
            throw "RX6 fixture $($fixture.Name) output mismatch. Expected '$expectedOutput'; got '$actualText'/$($actual.ExitCode)."
        }
        if ($null -ne $script:reginaCommand) {
            $reference = Invoke-CapturedProcess $script:reginaCommand.Source @($fixture.FullName)
            $referenceText = (($reference.Stdout + $reference.Stderr) -replace "`r`n", "`n").TrimEnd([char[]]"`n")
            if ($reference.ExitCode -ne 0 -or $referenceText -cne $expectedOutput) {
                throw "RX6 fixture $($fixture.Name) expected '$expectedOutput' from Regina; got '$referenceText'/$($reference.ExitCode)."
            }
            ++$script:rx6ReginaPassed
        }
    }
    ++$script:rx6Passed
    Write-Host "[PASS] RX6 fixture $($fixture.Name)"
}

$rx7FixtureDirectory = Join-Path $PSScriptRoot "rexx_rx7"
$script:rx7Passed = 0
foreach ($fixture in Get-ChildItem -LiteralPath $rx7FixtureDirectory -Filter "*.rex" | Sort-Object Name) {
    $diagnosticPath = [System.IO.Path]::ChangeExtension($fixture.FullName, ".diag")
    $outputPath = [System.IO.Path]::ChangeExtension($fixture.FullName, ".out")
    $actual = Invoke-CapturedProcess $cliTest @($fixture.FullName)
    $actualText = (($actual.Stdout + $actual.Stderr) -replace "`r`n", "`n").TrimEnd([char[]]"`n")
    if (Test-Path -LiteralPath $diagnosticPath) {
        $expectedDiagnostic = (Get-Content -Raw -LiteralPath $diagnosticPath).Trim()
        if ($actual.ExitCode -ne 1 -or $actual.Stdout.Length -ne 0 -or -not $actualText.Contains($expectedDiagnostic)) {
            throw "RX7 fixture $($fixture.Name) failed its expected diagnostic. Exit=$($actual.ExitCode) Output=$actualText"
        }
    } else {
        if (-not (Test-Path -LiteralPath $outputPath)) {
            throw "RX7 fixture $($fixture.Name) has no .out expectation."
        }
        $expectedOutput = ((Get-Content -Raw -LiteralPath $outputPath) -replace "`r`n", "`n").TrimEnd([char[]]"`n")
        $stdoutText = ($actual.Stdout -replace "`r`n", "`n").TrimEnd([char[]]"`n")
        if ($actual.ExitCode -ne 0 -or $stdoutText -cne $expectedOutput -or $actual.Stderr.Length -ne 0) {
            throw "RX7 fixture $($fixture.Name) output mismatch. Expected '$expectedOutput'; got '$actualText'/$($actual.ExitCode)."
        }
    }
    ++$script:rx7Passed
    Write-Host "[PASS] RX7 fixture $($fixture.Name)"
}

$rx8FixtureDirectory = Join-Path $PSScriptRoot "rexx_rx8"
$script:rx8Passed = 0
$script:rx8ReginaPassed = 0
foreach ($fixture in Get-ChildItem -LiteralPath $rx8FixtureDirectory -Filter "*.rex" | Sort-Object Name) {
    $diagnosticPath = [System.IO.Path]::ChangeExtension($fixture.FullName, ".diag")
    $outputPath = [System.IO.Path]::ChangeExtension($fixture.FullName, ".out")
    $actual = Invoke-CapturedProcess $cliTest @($fixture.FullName)
    $actualText = (($actual.Stdout + $actual.Stderr) -replace "`r`n", "`n").TrimEnd([char[]]"`n")
    if (Test-Path -LiteralPath $diagnosticPath) {
        $expectedDiagnostic = (Get-Content -Raw -LiteralPath $diagnosticPath).Trim()
        if ($actual.ExitCode -ne 1 -or $actual.Stdout.Length -ne 0 -or -not $actualText.Contains($expectedDiagnostic)) {
            throw "RX8 fixture $($fixture.Name) failed its expected diagnostic. Exit=$($actual.ExitCode) Output=$actualText"
        }
    } else {
        if (-not (Test-Path -LiteralPath $outputPath)) {
            throw "RX8 fixture $($fixture.Name) has no .out expectation."
        }
        $expectedOutput = ((Get-Content -Raw -LiteralPath $outputPath) -replace "`r`n", "`n").TrimEnd([char[]]"`n")
        $stdoutText = ($actual.Stdout -replace "`r`n", "`n").TrimEnd([char[]]"`n")
        if ($actual.ExitCode -ne 0 -or $stdoutText -cne $expectedOutput -or $actual.Stderr.Length -ne 0) {
            throw "RX8 fixture $($fixture.Name) output mismatch. Expected '$expectedOutput'; got '$actualText'/$($actual.ExitCode)."
        }
        if ($null -ne $script:reginaCommand) {
            $reference = Invoke-CapturedProcess $script:reginaCommand.Source @($fixture.FullName)
            $referenceText = (($reference.Stdout + $reference.Stderr) -replace "`r`n", "`n").TrimEnd([char[]]"`n")
            if ($reference.ExitCode -ne 0 -or $referenceText -cne $expectedOutput) {
                throw "RX8 fixture $($fixture.Name) expected '$expectedOutput' from Regina; got '$referenceText'/$($reference.ExitCode)."
            }
            ++$script:rx8ReginaPassed
        }
    }
    ++$script:rx8Passed
    Write-Host "[PASS] RX8 fixture $($fixture.Name)"
}

$rx9FixtureDirectory = Join-Path $PSScriptRoot "rexx_rx9"
$script:rx9Passed = 0
foreach ($fixture in Get-ChildItem -LiteralPath $rx9FixtureDirectory -Filter "*.rex" | Sort-Object Name) {
    $diagnosticPath = [System.IO.Path]::ChangeExtension($fixture.FullName, ".diag")
    $outputPath = [System.IO.Path]::ChangeExtension($fixture.FullName, ".out")
    $actual = Invoke-CapturedProcess $cliTest @($fixture.FullName)
    $actualText = (($actual.Stdout + $actual.Stderr) -replace "`r`n", "`n").TrimEnd([char[]]"`n")
    if (Test-Path -LiteralPath $diagnosticPath) {
        $expectedDiagnostic = (Get-Content -Raw -LiteralPath $diagnosticPath).Trim()
        if ($actual.ExitCode -ne 1 -or $actual.Stdout.Length -ne 0 -or -not $actualText.Contains($expectedDiagnostic)) {
            throw "RX9 fixture $($fixture.Name) failed its expected diagnostic. Exit=$($actual.ExitCode) Output=$actualText"
        }
    } else {
        if (-not (Test-Path -LiteralPath $outputPath)) {
            throw "RX9 fixture $($fixture.Name) has no .out expectation."
        }
        $expectedOutput = ((Get-Content -Raw -LiteralPath $outputPath) -replace "`r`n", "`n").TrimEnd([char[]]"`n")
        $stdoutText = ($actual.Stdout -replace "`r`n", "`n").TrimEnd([char[]]"`n")
        if ($actual.ExitCode -ne 0 -or $stdoutText -cne $expectedOutput -or $actual.Stderr.Length -ne 0) {
            throw "RX9 fixture $($fixture.Name) output mismatch. Expected '$expectedOutput'; got '$actualText'/$($actual.ExitCode)."
        }
    }
    ++$script:rx9Passed
    Write-Host "[PASS] RX9 fixture $($fixture.Name)"
}

$rx10FixtureDirectory = Join-Path $PSScriptRoot "rexx_rx10"
$script:rx10Passed = 0
foreach ($fixture in Get-ChildItem -LiteralPath $rx10FixtureDirectory -Filter "*.rex" | Sort-Object Name) {
    $diagnosticPath = [System.IO.Path]::ChangeExtension($fixture.FullName, ".diag")
    $outputPath = [System.IO.Path]::ChangeExtension($fixture.FullName, ".out")
    $actual = Invoke-CapturedProcess $cliTest @($fixture.FullName)
    $actualText = (($actual.Stdout + $actual.Stderr) -replace "`r`n", "`n").TrimEnd([char[]]"`n")
    if (Test-Path -LiteralPath $diagnosticPath) {
        $expectedDiagnostic = (Get-Content -Raw -LiteralPath $diagnosticPath).Trim()
        if ($actual.ExitCode -ne 1 -or $actual.Stdout.Length -ne 0 -or -not $actualText.Contains($expectedDiagnostic)) {
            throw "RX10 fixture $($fixture.Name) failed its expected diagnostic. Exit=$($actual.ExitCode) Output=$actualText"
        }
    } else {
        if (-not (Test-Path -LiteralPath $outputPath)) {
            throw "RX10 fixture $($fixture.Name) has no .out expectation."
        }
        $expectedOutput = ((Get-Content -Raw -LiteralPath $outputPath) -replace "`r`n", "`n").TrimEnd([char[]]"`n")
        $stdoutText = ($actual.Stdout -replace "`r`n", "`n").TrimEnd([char[]]"`n")
        if ($actual.ExitCode -ne 0 -or $stdoutText -cne $expectedOutput -or $actual.Stderr.Length -ne 0) {
            throw "RX10 fixture $($fixture.Name) output mismatch. Expected '$expectedOutput'; got '$actualText'/$($actual.ExitCode)."
        }
    }
    ++$script:rx10Passed
    Write-Host "[PASS] RX10 fixture $($fixture.Name)"
}

$rx12FixtureDirectory = Join-Path $PSScriptRoot "rexx_compat\cases\string_bifs"
$script:rx12FixturePassed = 0
foreach ($fixture in Get-ChildItem -LiteralPath $rx12FixtureDirectory -Filter "*.rex" | Sort-Object Name) {
    $diagnosticPath = [System.IO.Path]::ChangeExtension($fixture.FullName, ".diag")
    $outputPath = [System.IO.Path]::ChangeExtension($fixture.FullName, ".out")
    $actual = Invoke-CapturedProcess $cliTest @($fixture.FullName)
    $actualText = (($actual.Stdout + $actual.Stderr) -replace "`r`n", "`n").TrimEnd([char[]]"`n")
    if (Test-Path -LiteralPath $diagnosticPath) {
        $expectedDiagnostic = (Get-Content -Raw -LiteralPath $diagnosticPath).Trim()
        if ($actual.ExitCode -ne 1 -or $actual.Stdout.Length -ne 0 -or -not $actualText.Contains($expectedDiagnostic)) {
            throw "RX12 fixture $($fixture.Name) failed its expected diagnostic. Exit=$($actual.ExitCode) Output=$actualText"
        }
    } else {
        if (-not (Test-Path -LiteralPath $outputPath)) {
            throw "RX12 fixture $($fixture.Name) has no .out expectation."
        }
        $expectedOutput = ((Get-Content -Raw -LiteralPath $outputPath) -replace "`r`n", "`n").TrimEnd([char[]]"`n")
        $stdoutText = ($actual.Stdout -replace "`r`n", "`n").TrimEnd([char[]]"`n")
        if ($actual.ExitCode -ne 0 -or $stdoutText -cne $expectedOutput -or $actual.Stderr.Length -ne 0) {
            throw "RX12 fixture $($fixture.Name) output mismatch. Expected '$expectedOutput'; got '$actualText'/$($actual.ExitCode)."
        }
    }
    ++$script:rx12FixturePassed
    Write-Host "[PASS] RX12 fixture $($fixture.Name)"
}

$parserLimitPath = Join-Path $buildDirectory "parse_template_node_limit.rex"
$parserLimitSource = "parse value '' with " + ((@('.') * 65540) -join ' ')
[System.IO.File]::WriteAllText(
    $parserLimitPath,
    $parserLimitSource,
    [System.Text.UTF8Encoding]::new($false))
$parserLimitResult = Invoke-CapturedProcess $cliTest @($parserLimitPath)
$parserLimitOutput = $parserLimitResult.Stdout + $parserLimitResult.Stderr
if ($parserLimitResult.ExitCode -ne 1 -or
    $parserLimitResult.Stdout.Length -ne 0 -or
    -not $parserLimitOutput.Contains("parser resource limit exceeded")) {
    throw "PARSE template-node limit did not fail with a parser diagnostic. Exit=$($parserLimitResult.ExitCode) Output=$parserLimitOutput"
}
$script:rx6Passed++
Write-Host "[PASS] RX6 parser template-node resource limit"

$parseWorkPath = Join-Path $buildDirectory "parse_match_work_limit.rex"
$parseWorkSource = "parse value '" + ('a' * 8192) + "' with field '" +
    (('a' * 4095) + 'b') + "' rest`n"
[System.IO.File]::WriteAllText(
    $parseWorkPath,
    $parseWorkSource,
    [System.Text.UTF8Encoding]::new($false))
$parseWorkResult = Invoke-CapturedProcess $cliTest @($parseWorkPath)
$parseWorkOutput = $parseWorkResult.Stdout + $parseWorkResult.Stderr
if ($parseWorkResult.ExitCode -ne 1 -or
    $parseWorkResult.Stdout.Length -ne 0 -or
    -not $parseWorkOutput.Contains("VM resource limit exceeded")) {
    throw "PARSE matching-work limit did not fail safely. Exit=$($parseWorkResult.ExitCode) Output=$parseWorkOutput"
}
$script:rx6Passed++
Write-Host "[PASS] RX6 bounded pattern-matching work"

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
$limitResult = Invoke-CapturedProcess $cliTest @($oversizePath)
$limitOutput = $limitResult.Stdout + $limitResult.Stderr
$limitExit = $limitResult.ExitCode
if ($limitExit -ne 1 -or
    -not $limitOutput.Contains("file exceeds the 16 MiB limit")) {
    throw "Source-size limit did not fail safely. Exit=$limitExit Output=$limitOutput"
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
Write-Host "RX9 condition fixtures passed: $script:rx9Passed/$script:rx9Passed"
Write-Host "RX10 INTERPRET fixtures passed: $script:rx10Passed/$script:rx10Passed"
Write-Host "RX12 classic built-in fixtures passed: $script:rx12FixturePassed/$script:rx12FixturePassed"
Write-Host "RX3 compatibility fixtures passed: $script:rx3Passed/$script:rx3Passed"
Write-Host "RX4 compatibility fixtures passed: $script:rx4Passed/$script:rx4Passed"
Write-Host "RX5 compatibility fixtures passed: $script:rx5Passed/$script:rx5Passed"
Write-Host "RX6 compatibility and resource fixtures passed: $script:rx6Passed/$script:rx6Passed"
Write-Host "RX7 compatibility and resource fixtures passed: $script:rx7Passed/$script:rx7Passed"
Write-Host "RX8 compatibility fixtures passed: $script:rx8Passed/$script:rx8Passed"
if ($null -eq $script:reginaCommand) {
    Write-Host "RX3 Regina differential cases: 0 run (Regina unavailable)"
    Write-Host "RX4 Regina differential cases: 0 run (Regina unavailable)"
    Write-Host "RX5 Regina differential cases: 0 run (Regina unavailable)"
    Write-Host "RX6 Regina differential cases: 0 run (Regina unavailable)"
    Write-Host "RX8 Regina differential fixtures: 0 run (Regina unavailable)"
} else {
    Write-Host "RX3 Regina differential cases passed: $script:rx3ReginaPassed/$script:rx3ReginaPassed"
    Write-Host "RX4 Regina differential cases passed: $script:rx4ReginaPassed/$script:rx4ReginaPassed"
    Write-Host "RX5 Regina differential cases passed: $script:rx5ReginaPassed/$script:rx5ReginaPassed"
    Write-Host "RX6 Regina differential cases passed: $script:rx6ReginaPassed/$script:rx6ReginaPassed"
    Write-Host "RX8 Regina differential fixtures passed: $script:rx8ReginaPassed/$script:rx8ReginaPassed"
}
Write-Host "Build and temporary script artifacts: $buildDirectory"
