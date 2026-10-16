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
        [switch]$CompareRegina
    )

    $sourcePath = Join-Path $buildDirectory ($Name + ".rex")
    [System.IO.File]::WriteAllText(
        $sourcePath,
        $Source,
        [System.Text.UTF8Encoding]::new($false))

    $actual = Invoke-CapturedProcess $cliTest @($sourcePath)
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

function Invoke-CapturedProcess {
    param(
        [string]$Executable,
        [string[]]$Arguments
    )

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Executable
    $startInfo.Arguments = (($Arguments | ForEach-Object { '"' + $_ + '"' }) -join ' ')
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
Write-Host "RX3 compatibility fixtures passed: $script:rx3Passed/$script:rx3Passed"
Write-Host "RX4 compatibility fixtures passed: $script:rx4Passed/$script:rx4Passed"
Write-Host "RX5 compatibility fixtures passed: $script:rx5Passed/$script:rx5Passed"
if ($null -eq $script:reginaCommand) {
    Write-Host "RX3 Regina differential cases: 0 run (Regina unavailable)"
    Write-Host "RX4 Regina differential cases: 0 run (Regina unavailable)"
    Write-Host "RX5 Regina differential cases: 0 run (Regina unavailable)"
} else {
    Write-Host "RX3 Regina differential cases passed: $script:rx3ReginaPassed/$script:rx3ReginaPassed"
    Write-Host "RX4 Regina differential cases passed: $script:rx4ReginaPassed/$script:rx4ReginaPassed"
    Write-Host "RX5 Regina differential cases passed: $script:rx5ReginaPassed/$script:rx5ReginaPassed"
}
Write-Host "Build and temporary script artifacts: $buildDirectory"
