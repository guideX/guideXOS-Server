param(
    [ValidateSet('RX12R1','RX13','RX14','RX15','RX16','RX17','RX18','RX19','RX20','RX21','RX22','RX24','RX25','RX26','RX27','RX28','RX29','RX30','RX31','RX32','RX33','RX34','RX35','RX37')]
    [string]$Phase = 'RX12R1',
    [ValidateSet('Address', 'Undefined', 'Both')]
    [string]$Sanitizer = 'Both',
    [string]$OutputRoot = ''
)

$ErrorActionPreference = 'Stop'
$rootDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ((git -C $rootDir branch --show-current).Trim() -ne 'REXX_SUPPORT') {
    throw "$Phase sanitizer qualification requires branch REXX_SUPPORT."
}
$phaseOutputRoot = if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    Join-Path $rootDir "build\rexx\$($Phase.ToLowerInvariant())"
} else {
    [System.IO.Path]::GetFullPath($OutputRoot)
}
$guardModule = Join-Path $PSScriptRoot 'rexx_artifact_guard.psm1'
Import-Module -Name $guardModule -Force
$guardManifest = if ($Phase -eq 'RX30') {
    Join-Path $rootDir 'docs\rexx\evidence\rx30\protected-artifacts-guard.json'
} elseif ($Phase -eq 'RX31') {
    Join-Path $rootDir 'docs\rexx\evidence\rx31\protected-artifacts-guard.json'
} elseif ($Phase -eq 'RX32') {
    Join-Path $rootDir 'docs\rexx\evidence\rx32\protected-artifacts-guard.json'
} elseif ($Phase -eq 'RX33') {
    Join-Path $rootDir 'docs\rexx\evidence\rx33\protected-artifacts-guard.json'
} elseif ($Phase -eq 'RX34') {
    Join-Path $rootDir 'docs\rexx\evidence\rx34\protected-artifacts-guard.json'
} elseif ($Phase -eq 'RX35') {
    Join-Path $rootDir 'docs\rexx\evidence\rx35\protected-artifacts-guard.json'
} elseif ($Phase -eq 'RX37') {
    Join-Path $rootDir 'docs\rexx\evidence\rx37\protected-artifacts-guard.json'
} elseif ($Phase -eq 'RX29') {
    Join-Path $rootDir 'docs\rexx\evidence\rx29\protected-artifacts-guard.json'
} elseif ($Phase -eq 'RX28') {
    Join-Path $rootDir 'docs\rexx\evidence\rx28\protected-artifacts-start.json'
} elseif ($Phase -eq 'RX27') {
    Join-Path $rootDir 'docs\rexx\evidence\rx27\protected-artifacts-start.json'
} elseif ($Phase -eq 'RX26') {
    Join-Path $rootDir 'docs\rexx\evidence\rx26\protected-artifacts-start.json'
} elseif ($Phase -eq 'RX25') {
    Join-Path $rootDir 'docs\rexx\evidence\rx25\protected-artifacts-start.json'
} elseif ($Phase -eq 'RX24') {
    Join-Path $rootDir 'docs\rexx\evidence\rx24\protected-artifacts-start.json'
} elseif ($Phase -eq 'RX22') {
    Join-Path $rootDir 'docs\rexx\evidence\rx22\protected-artifacts-start.json'
} elseif ($Phase -eq 'RX21') {
    Join-Path $rootDir 'docs\rexx\evidence\rx21\protected-artifacts-start.json'
} elseif ($Phase -eq 'RX19') {
    Join-Path $rootDir 'docs\rexx\evidence\rx19\protected-artifacts-start.json'
} elseif ($Phase -eq 'RX20') {
    Join-Path $rootDir 'docs\rexx\evidence\rx20\protected-artifacts-start.json'
} elseif ($Phase -eq 'RX18') {
    Join-Path $rootDir 'docs\rexx\evidence\rx18\protected-artifacts-start.json'
} elseif ($Phase -eq 'RX17') {
    Join-Path $rootDir 'docs\rexx\evidence\rx17\protected-artifacts-start.json'
} elseif ($Phase -eq 'RX16') {
    Join-Path $rootDir 'docs\rexx\evidence\rx16\protected-artifacts-start.json'
} elseif ($Phase -eq 'RX15') {
    Join-Path $rootDir 'docs\rexx\evidence\rx15\protected-artifacts-start.json'
} elseif ($Phase -eq 'RX14') {
    Join-Path $rootDir 'docs\rexx\evidence\rx14\protected-artifacts-start.json'
} else {
    Join-Path $rootDir 'docs\rexx\evidence\rx12r1\protected-artifacts-baseline.json'
}
$script:workingDirectory = ''

function Import-VisualStudioEnvironment {
    $vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw "Visual Studio vswhere.exe was not found at $vswhere."
    }
    $vswhereOutput = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    $vswhereExitCode = $LASTEXITCODE
    $installationPath = @($vswhereOutput | Select-Object -First 1)[0]
    if (($null -ne $vswhereExitCode -and $vswhereExitCode -ne 0) -or [string]::IsNullOrWhiteSpace($installationPath)) {
        throw 'No Visual Studio installation with C++ build tools was discovered.'
    }
    $devCmd = Join-Path $installationPath 'Common7\Tools\VsDevCmd.bat'
    if (-not (Test-Path -LiteralPath $devCmd)) {
        throw "Visual Studio developer environment script was not found: $devCmd"
    }

    $command = 'call "{0}" -arch=amd64 -host_arch=amd64 -no_logo >nul && set' -f $devCmd
    $environmentLines = & $env:ComSpec /d /c $command
    if ($LASTEXITCODE -ne 0) {
        throw "Visual Studio environment initialization failed with exit code $LASTEXITCODE."
    }
    foreach ($line in $environmentLines) {
        $separator = $line.IndexOf('=')
        if ($separator -gt 0) {
            $name = $line.Substring(0, $separator)
            $value = $line.Substring($separator + 1)
            [Environment]::SetEnvironmentVariable($name, $value, 'Process')
        }
    }
    return $installationPath
}

function Invoke-Captured {
    param(
        [string]$Executable,
        [string[]]$Arguments,
        [int]$TimeoutSeconds = 120
    )
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo.FileName = $Executable
    $process.StartInfo.UseShellExecute = $false
    if (-not [string]::IsNullOrWhiteSpace($script:workingDirectory)) {
        $process.StartInfo.WorkingDirectory = $script:workingDirectory
    }
    $process.StartInfo.CreateNoWindow = $true
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true
    if ($Arguments.Count -eq 0) {
        $process.StartInfo.Arguments = ''
    } else {
        $quotedArguments = foreach ($argument in $Arguments) {
            ConvertTo-WindowsProcessArgument $argument
        }
        $process.StartInfo.Arguments = [string]::Join(' ', [string[]]$quotedArguments)
    }
    [void]$process.Start()
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $completed = $process.WaitForExit($TimeoutSeconds * 1000)
    if (-not $completed) {
        $process.Kill($true)
        $process.WaitForExit()
    }
    return [PSCustomObject]@{
        ExitCode = if ($completed) { $process.ExitCode } else { $null }
        TimedOut = -not $completed
        Stdout = $stdoutTask.GetAwaiter().GetResult()
        Stderr = $stderrTask.GetAwaiter().GetResult()
    }
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

function Write-ProcessLog {
    param([string]$BasePath, $Result)
    [System.IO.File]::WriteAllText($BasePath + '.stdout.log', $Result.Stdout, [System.Text.UTF8Encoding]::new($false))
    [System.IO.File]::WriteAllText($BasePath + '.stderr.log', $Result.Stderr, [System.Text.UTF8Encoding]::new($false))
    [System.IO.File]::WriteAllText(
        $BasePath + '.status.json',
        (ConvertTo-Json -InputObject ([PSCustomObject]@{ exit_code = $Result.ExitCode; timed_out = $Result.TimedOut }) -Depth 4) + "`n",
        [System.Text.UTF8Encoding]::new($false))
}

function Invoke-OneSanitizer {
    param([ValidateSet('Address', 'Undefined')][string]$Kind)

    $runId = [Guid]::NewGuid().ToString('N')
    $runDir = Join-Path $phaseOutputRoot "sanitizer-$($Kind.ToLowerInvariant())-$runId"
    $evidenceDir = Join-Path $runDir 'logs'

    $installationPath = Import-VisualStudioEnvironment
    $vcToolsVersion = $env:VCToolsVersion
    $vcToolsRoot = $env:VCToolsInstallDir
    if ([string]::IsNullOrWhiteSpace($vcToolsRoot)) {
        throw 'VCToolsInstallDir was not provided by the Visual Studio developer environment.'
    }
    $compiler = if ($Kind -eq 'Address') {
        (Get-Command cl.exe -ErrorAction Stop).Source
    } else {
        'C:\Program Files\LLVM\bin\clang-cl.exe'
    }
    if (-not (Test-Path -LiteralPath $compiler)) {
        throw "$Kind sanitizer compiler not found: $compiler"
    }
    if ($Kind -eq 'Address') {
        $runtimeDir = Join-Path $vcToolsRoot 'bin\Hostx64\x64'
        $runtimeDll = Join-Path $runtimeDir 'clang_rt.asan_dynamic-x86_64.dll'
        if (-not (Test-Path -LiteralPath $runtimeDll)) {
            throw "MSVC AddressSanitizer runtime DLL is missing: $runtimeDll"
        }
        $env:PATH = $runtimeDir + ';' + $env:PATH
        $env:ASAN_OPTIONS = 'halt_on_error=1'
    } else {
        $runtimeLib = Join-Path $vcToolsRoot 'lib\x64\clang_rt.ubsan_standalone-x86_64.lib'
        if (-not (Test-Path -LiteralPath $runtimeLib)) {
            throw "Clang UBSan runtime library is missing: $runtimeLib"
        }
        $env:UBSAN_OPTIONS = 'halt_on_error=1:print_stacktrace=1'
    }

    $common = @(
        'rexx_lexer.c', 'rexx_parser.c', 'rexx_compiler.c', 'rexx_data_stack.c', 'rexx_vm.c',
        'rexx_builtin.c', 'rexx_numeric.c'
    ) | ForEach-Object { Join-Path $rootDir $_ }
    $cliSources = @(
        (Join-Path $rootDir 'tests\rexx_cli_test_driver.c'),
        (Join-Path $rootDir 'rexx_cli.c'),
        (Join-Path $rootDir 'rexx_os_io.c'),
        (Join-Path $rootDir 'rexx_output.c')
    ) + $common
    $cliSupport = @(
        (Join-Path $rootDir 'rexx_cli.c'),
        (Join-Path $rootDir 'rexx_os_io.c'),
        (Join-Path $rootDir 'rexx_output.c')
    ) + $common

    $plans = @(
        [PSCustomObject]@{ Name = 'rx1_runtime'; Driver = 'rexx_main.c'; Sources = $common; Defines = @() },
        [PSCustomObject]@{ Name = 'rx1_cli'; Driver = 'tests\rexx_cli_test_driver.c'; Sources = $cliSupport; Defines = @(); RunArgs = @('--help') },
        [PSCustomObject]@{ Name = 'rx2_vfs'; Driver = 'tests\rexx_rx2_test_driver.c'; Sources = $cliSupport; Defines = @() },
        [PSCustomObject]@{ Name = 'rx3_decimal'; Driver = 'tests\rexx_rx3_decimal_test.c'; Sources = @((Join-Path $rootDir 'rexx_numeric.c')); Defines = @() },
        [PSCustomObject]@{ Name = 'rx3_alloc'; Driver = 'tests\rexx_rx3_alloc_failure_test.c'; Sources = $common; Defines = @('REXX_VM_TEST_ALLOCATOR') },
        [PSCustomObject]@{ Name = 'rx4_arithmetic'; Driver = 'tests\rexx_rx4_arithmetic_test.c'; Sources = $common; Defines = @() },
        [PSCustomObject]@{ Name = 'rx5_numeric_context'; Driver = 'tests\rexx_rx5_numeric_context_test.c'; Sources = $common; Defines = @() },
        [PSCustomObject]@{ Name = 'rx6_parse_alloc'; Driver = 'tests\rexx_rx6_parse_alloc_failure_test.c'; Sources = $common; Defines = @('REXX_VM_TEST_ALLOCATOR','REXX_PARSER_TEST_ALLOCATOR','REXX_COMPILER_TEST_ALLOCATOR') },
        [PSCustomObject]@{ Name = 'rx7_routines'; Driver = 'tests\rexx_rx7_routines_test.c'; Sources = $common; Defines = @() },
        [PSCustomObject]@{ Name = 'rx7_alloc'; Driver = 'tests\rexx_rx7_alloc_failure_test.c'; Sources = $common; Defines = @('REXX_VM_TEST_ALLOCATOR','REXX_PARSER_TEST_ALLOCATOR','REXX_COMPILER_TEST_ALLOCATOR') },
        [PSCustomObject]@{ Name = 'rx8_semantics_alloc'; Driver = 'tests\rexx_rx8_semantics_alloc_test.c'; Sources = $common; Defines = @('REXX_VM_TEST_ALLOCATOR','REXX_PARSER_TEST_ALLOCATOR','REXX_COMPILER_TEST_ALLOCATOR') },
        [PSCustomObject]@{ Name = 'rx9_conditions'; Driver = 'tests\rexx_rx9_condition_test.c'; Sources = $common; Defines = @('REXX_VM_TEST_ALLOCATOR','REXX_PARSER_TEST_ALLOCATOR','REXX_COMPILER_TEST_ALLOCATOR') },
        [PSCustomObject]@{ Name = 'rx10_interpret'; Driver = 'tests\rexx_rx10_interpret_test.c'; Sources = $common; Defines = @('REXX_VM_TEST_ALLOCATOR','REXX_PARSER_TEST_ALLOCATOR','REXX_COMPILER_TEST_ALLOCATOR') },
        [PSCustomObject]@{ Name = 'rx12_builtins'; Driver = 'tests\rexx_rx12_builtins_test.c'; Sources = $common; Defines = @('REXX_VM_TEST_ALLOCATOR') },
        [PSCustomObject]@{ Name = 'rx13_string_editing'; Driver = 'tests\rexx_rx13_string_editing_test.c'; Sources = $common; Defines = @('REXX_VM_TEST_ALLOCATOR') },
        [PSCustomObject]@{ Name = 'rx14_numeric_inspection'; Driver = 'tests\rexx_rx14_numeric_inspection_test.c'; Sources = $common; Defines = @('REXX_VM_TEST_ALLOCATOR') },
        [PSCustomObject]@{ Name = 'rx15_conversion'; Driver = 'tests\rexx_rx15_conversion_test.c'; Sources = $common; Defines = @('REXX_VM_TEST_ALLOCATOR') },
        [PSCustomObject]@{ Name = 'rx16_literals'; Driver = 'tests\rexx_rx16_literal_test.c'; Sources = $common; Defines = @('REXX_VM_TEST_ALLOCATOR','REXX_PARSER_TEST_ALLOCATOR','REXX_COMPILER_TEST_ALLOCATOR') },
        [PSCustomObject]@{ Name = 'rx17_bitwise'; Driver = 'tests\rexx_rx17_bitwise_test.c'; Sources = $common; Defines = @('REXX_VM_TEST_ALLOCATOR') },
        [PSCustomObject]@{ Name = 'rx18_string_builtins'; Driver = 'tests\rexx_rx18_string_builtins_test.c'; Sources = $common; Defines = @('REXX_VM_TEST_ALLOCATOR') },
        [PSCustomObject]@{ Name = 'rx19_date_time'; Driver = 'tests\rexx_rx19_date_time_test.c'; Sources = $common; Defines = @('REXX_VM_TEST_ALLOCATOR') },
        [PSCustomObject]@{ Name = 'rx22_classic_diagnostic'; Driver = 'tests\rexx_rx22_classic_diagnostic_test.c'; Sources = $common; Defines = @('REXX_VM_TEST_ALLOCATOR','REXX_COMPILER_TEST_ALLOCATOR') }
    )
    if ($Phase -in @('RX24','RX26','RX27','RX28','RX29','RX30','RX31','RX32','RX33','RX34','RX35','RX37')) {
        $plans += [PSCustomObject]@{
            Name = 'rx24_address_trace'
            Driver = 'tests\rexx_rx24_address_trace_test.c'
            Sources = $common
            Defines = @('REXX_VM_TEST_ALLOCATOR')
        }
    }
    if ($Phase -in @('RX25','RX26','RX27','RX28','RX29','RX30','RX31','RX32','RX33','RX34','RX35','RX37')) {
        $plans += [PSCustomObject]@{
            Name = 'rx25_random'
            Driver = 'tests\rexx_rx25_random_test.c'
            Sources = $common + @((Join-Path $rootDir 'rexx_os_io.c'), (Join-Path $rootDir 'rexx_output.c'))
            Defines = @('REXX_VM_TEST_ALLOCATOR')
        }
    }
    if ($Phase -in @('RX26','RX27','RX28','RX29','RX30','RX31','RX32','RX33','RX34','RX35','RX37')) {
        $plans += [PSCustomObject]@{
            Name = 'rx26_index'
            Driver = 'tests\rexx_rx26_index_test.c'
            Sources = $common
            Defines = @('REXX_VM_TEST_ALLOCATOR')
        }
    }
    if ($Phase -in @('RX27','RX28','RX29','RX30','RX31','RX32','RX33','RX34','RX35','RX37')) {
        $plans += [PSCustomObject]@{
            Name = 'rx27_justify'
            Driver = 'tests\rexx_rx27_justify_test.c'
            Sources = $common
            Defines = @('REXX_VM_TEST_ALLOCATOR')
        }
    }
    if ($Phase -in @('RX28','RX29','RX30','RX31','RX32','RX33','RX34','RX35','RX37')) {
        $plans += [PSCustomObject]@{
            Name = 'rx28_find'
            Driver = 'tests\rexx_rx28_find_test.c'
            Sources = $common
            Defines = @('REXX_VM_TEST_ALLOCATOR')
        }
    }
    if ($Phase -in @('RX30','RX31','RX32','RX33','RX34','RX35','RX37')) {
        $plans += [PSCustomObject]@{
            Name = 'rx30_data_stack'
            Driver = 'tests\rexx_rx30_data_stack_test.c'
            Sources = @((Join-Path $rootDir 'rexx_data_stack.c'))
            Defines = @()
        }
        $plans += [PSCustomObject]@{
            Name = 'rx30_queue_language'
            Driver = 'tests\rexx_rx30_queue_test.c'
            Sources = $common
            Defines = @('REXX_VM_TEST_ALLOCATOR','REXX_PARSER_TEST_ALLOCATOR','REXX_COMPILER_TEST_ALLOCATOR')
        }
    }
    if ($Phase -in @('RX31','RX32','RX33','RX34','RX35','RX37')) {
        $plans += [PSCustomObject]@{
            Name = 'rx31_host_input'
            Driver = 'tests\rexx_rx31_host_input_test.c'
            Sources = $common
            Defines = @('REXX_VM_TEST_ALLOCATOR','REXX_PARSER_TEST_ALLOCATOR','REXX_COMPILER_TEST_ALLOCATOR')
        }
    }
    if ($Phase -in @('RX32','RX33','RX34','RX35','RX37')) {
        $plans += [PSCustomObject]@{
            Name = 'rx32_value'
            Driver = 'tests\rexx_rx32_value_test.c'
            Sources = $common
            Defines = @('REXX_VM_TEST_ALLOCATOR','REXX_PARSER_TEST_ALLOCATOR','REXX_COMPILER_TEST_ALLOCATOR')
        }
    }
    if ($Phase -in @('RX33','RX34','RX35','RX37')) {
        $rx34ByteStreamDefines = @(
            'REXX_VM_TEST_ALLOCATOR',
            'REXX_PARSER_TEST_ALLOCATOR',
            'REXX_COMPILER_TEST_ALLOCATOR'
        )
        if ($Phase -eq 'RX34') { $rx34ByteStreamDefines += 'REXX_RX34_TEST' }
        $plans += [PSCustomObject]@{
            Name = 'rx33_byte_stream'
            Driver = 'tests\rexx_rx33_byte_stream_test.c'
            Sources = $common
            Defines = $rx34ByteStreamDefines
        }
    }
    if ($Phase -in @('RX34','RX35','RX37')) {
        $plans += [PSCustomObject]@{
            Name = 'rx34_stream_state'
            Driver = 'tests\rexx_rx34_stream_state_test.c'
            Sources = $common
            Defines = @('REXX_VM_TEST_ALLOCATOR','REXX_PARSER_TEST_ALLOCATOR','REXX_COMPILER_TEST_ALLOCATOR')
        }
    }
    if ($Phase -in @('RX35','RX37')) {
        $plans += [PSCustomObject]@{
            Name = 'rx35_externals'
            Driver = 'tests\rexx_rx35_externals_test.c'
            Sources = $common
            Defines = @('REXX_VM_TEST_ALLOCATOR','REXX_PARSER_TEST_ALLOCATOR','REXX_COMPILER_TEST_ALLOCATOR')
        }
    }
    if ($Phase -eq 'RX37') {
        $plans += [PSCustomObject]@{
            Name = 'rx37_output_capability'
            Driver = 'tests\rexx_rx37_output_capability_test.c'
            Sources = $common
            Defines = @('REXX_VM_TEST_ALLOCATOR')
        }
    }
    if ($Phase -in @('RX20','RX21','RX22','RX24','RX25','RX26','RX27','RX28','RX29','RX30','RX31','RX32','RX33','RX34','RX35','RX37')) {
        $plans += [PSCustomObject]@{
            Name = 'rx20_clock_contract'
            Driver = 'tests\kernel_clock_contract_test.cpp'
            Sources = @((Join-Path $rootDir 'kernel\core\clock_contract.cpp'))
            Defines = @()
            Cpp = $true
            NoKernelInclude = $true
        }
    }
    $buildDir = Join-Path $runDir 'bin'
    $workingDirectory = Join-Path $runDir 'temp'
    $plannedOutputs = [System.Collections.Generic.List[string]]::new()
    foreach ($plan in $plans) {
        $planOutputDir = Join-Path $buildDir $plan.Name
        $exePath = Join-Path $planOutputDir ($plan.Name + '.exe')
        $null = $plannedOutputs.Add($exePath)
        $null = $plannedOutputs.Add((Join-Path $planOutputDir ($plan.Name + '.compiler.pdb')))
        $null = $plannedOutputs.Add((Join-Path $planOutputDir ($plan.Name + '.link.pdb')))
        $sourcePaths = @((Join-Path $rootDir $plan.Driver)) + @($plan.Sources)
        foreach ($sourcePath in $sourcePaths) {
            $null = $plannedOutputs.Add((Join-Path $planOutputDir ([System.IO.Path]::GetFileNameWithoutExtension($sourcePath) + '.obj')))
        }
    }
    $null = Start-ReXXArtifactRun -ManifestPath $guardManifest -RunRoot $runDir `
        -WorkingDirectory $workingDirectory -PlannedOutputs @($plannedOutputs) -Phase $Phase
    New-Item -ItemType Directory -Path $evidenceDir | Out-Null
    $env:TEMP = $workingDirectory
    $env:TMP = $workingDirectory
    $env:TMPDIR = $workingDirectory
    $script:workingDirectory = $workingDirectory

    $compilerVersion = if ($Kind -eq 'Address') {
        $clVersion = Invoke-Captured -Executable $compiler -Arguments @('/Bv') -TimeoutSeconds 20
        $clVersion.Stdout + $clVersion.Stderr
    } else {
        $clangVersion = Invoke-Captured -Executable $compiler -Arguments @('--version') -TimeoutSeconds 20
        $clangVersion.Stdout + $clangVersion.Stderr
    }
    [System.IO.File]::WriteAllText((Join-Path $evidenceDir 'compiler-version.log'), $compilerVersion, [System.Text.UTF8Encoding]::new($false))
    Write-Host "$Phase $Kind sanitizer: $compiler"

    $built = @{}
    foreach ($plan in $plans) {
        $isCpp = $plan.PSObject.Properties.Name -contains 'Cpp' -and $plan.Cpp
        $planOutputDir = Join-Path $buildDir $plan.Name
        New-Item -ItemType Directory -Path $planOutputDir | Out-Null
        $sourcePaths = @((Join-Path $rootDir $plan.Driver)) + @($plan.Sources)
        $exeName = $plan.Name + '.exe'
        $exePath = Join-Path $planOutputDir $exeName
        $compilerArgs = @('/nologo')
        if ($isCpp) {
            $compilerArgs += @('/TP', '/std:c++14', '/EHsc')
            if (-not ($plan.PSObject.Properties.Name -contains 'NoKernelInclude' -and $plan.NoKernelInclude)) {
                $compilerArgs += '/I' + (Join-Path $rootDir 'kernel\core\include')
            }
        } else {
            $compilerArgs += @('/TC', '/std:c11', '/D_CRT_SECURE_NO_WARNINGS')
            if ($plan.Name -in @('rx16_literals','rx17_bitwise')) {
                $compilerArgs += '/I' + $rootDir
            }
        }
        $runtimeModel = if ($Kind -eq 'Undefined' -and $isCpp) { '/MT' } else { '/MD' }
        $compilerArgs += @($runtimeModel, '/Zi', '/Od')
        # All cl.exe and linker outputs are explicit and confined to this unique plan directory.
        $compilerArgs += '/Fo:' + $planOutputDir + [System.IO.Path]::DirectorySeparatorChar
        $compilerArgs += '/Fd:' + (Join-Path $planOutputDir ($plan.Name + '.compiler.pdb'))
        if ($Kind -eq 'Address') {
            $compilerArgs += '/fsanitize=address'
        } else {
            $compilerArgs += @('-fsanitize=undefined', '-fno-sanitize-recover=all')
        }
        foreach ($define in $plan.Defines) { $compilerArgs += '/D' + $define }
        $compilerArgs += $sourcePaths
        $compilerArgs += '/Fe:' + $exePath
        $compilerArgs += @('/link', '/INCREMENTAL:NO', ('/PDB:' + (Join-Path $planOutputDir ($plan.Name + '.link.pdb'))))

        $compileResult = Invoke-Captured -Executable $compiler -Arguments $compilerArgs -TimeoutSeconds 180
        Write-ProcessLog -BasePath (Join-Path $evidenceDir ($plan.Name + '.build')) -Result $compileResult
        if ($compileResult.TimedOut -or $compileResult.ExitCode -ne 0) {
            throw "$Kind sanitizer build failed for $($plan.Name). See $evidenceDir. `n$($compileResult.Stdout)`n$($compileResult.Stderr)"
        }
        $built[$plan.Name] = $exePath
        Write-Host "[BUILD PASS] $($plan.Name)"
    }

    Test-ReXXArtifactRun -ManifestPath $guardManifest -RunRoot $runDir -Stage "$($Kind.ToLowerInvariant())-builds" -Phase $Phase | Out-Null

    foreach ($plan in $plans) {
        $runArguments = if ($plan.PSObject.Properties.Name -contains 'RunArgs') { $plan.RunArgs } else { @() }
        $runResult = Invoke-Captured -Executable $built[$plan.Name] -Arguments $runArguments -TimeoutSeconds 90
        Write-ProcessLog -BasePath (Join-Path $evidenceDir ($plan.Name + '.run')) -Result $runResult
        if ($runResult.TimedOut -or $runResult.ExitCode -ne 0) {
            throw "$Kind sanitizer test failed for $($plan.Name). See $evidenceDir. `n$($runResult.Stdout)`n$($runResult.Stderr)"
        }
        Write-Host "[TEST PASS] $($plan.Name)"
    }

    Test-ReXXArtifactRun -ManifestPath $guardManifest -RunRoot $runDir -Stage "$($Kind.ToLowerInvariant())-hosted-tests" -Phase $Phase | Out-Null

    $python = Get-Command python -ErrorAction Stop
    $differentialDir = Join-Path $runDir 'differential'
    $differentialArgs = @(
        (Join-Path $PSScriptRoot 'rexx_compat\run_differential.py'),
        '--cli', $built['rx1_cli'],
        '--results-dir', $differentialDir
    )
    $diffResult = Invoke-Captured -Executable $python.Source -Arguments $differentialArgs -TimeoutSeconds 300
    Write-ProcessLog -BasePath (Join-Path $evidenceDir 'differential.run') -Result $diffResult
    if ($diffResult.TimedOut -or $diffResult.ExitCode -ne 0) {
        throw "$Kind sanitizer differential corpus execution failed. See $differentialDir and $evidenceDir. `n$($diffResult.Stdout)`n$($diffResult.Stderr)"
    }
    Write-Host $diffResult.Stdout.TrimEnd()
    Test-ReXXArtifactRun -ManifestPath $guardManifest -RunRoot $runDir -Stage "$($Kind.ToLowerInvariant())-corpus" -Phase $Phase | Out-Null

    $durableEvidence = Join-Path $rootDir ("docs\rexx\evidence\$($Phase.ToLowerInvariant())\sanitizer-$($Kind.ToLowerInvariant())-$runId")
    New-Item -ItemType Directory -Path $durableEvidence | Out-Null
    Copy-Item -LiteralPath $evidenceDir -Destination (Join-Path $durableEvidence 'logs') -Recurse
    Copy-Item -LiteralPath (Join-Path $runDir 'preservation') -Destination (Join-Path $durableEvidence 'preservation') -Recurse
    Copy-Item -LiteralPath (Join-Path $differentialDir 'results.json') -Destination $durableEvidence
    Copy-Item -LiteralPath (Join-Path $differentialDir 'raw') -Destination $durableEvidence -Recurse
    Write-Host "Evidence: $evidenceDir"
    Write-Host "Builds: $buildDir"
    Write-Host "Durable evidence: $durableEvidence"
    return [PSCustomObject]@{ Kind = $Kind; Compiler = $compiler; CompilerVersion = $compilerVersion.Split("`n")[0].Trim(); EvidenceDir = $evidenceDir; BuildDir = $buildDir; RunDir = $runDir; DurableEvidence = $durableEvidence; InstallationPath = $installationPath; VCToolsVersion = $vcToolsVersion }
}

$selected = if ($Sanitizer -eq 'Both') { @('Address', 'Undefined') } else { @($Sanitizer) }
$summaries = foreach ($kind in $selected) { Invoke-OneSanitizer -Kind $kind }
$summaryPath = Join-Path $summaries[-1].RunDir 'sanitizer-summary.json'
$summaries | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $summaryPath -Encoding utf8
Copy-Item -LiteralPath $summaryPath -Destination (Join-Path $rootDir "docs\rexx\evidence\$($Phase.ToLowerInvariant())\sanitizers-summary.json") -Force
Write-Output "Sanitizer summary: $summaryPath"
