$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$modulePath = Join-Path $repoRoot 'scripts\qac-managed-qemu.psm1'
$runnerPath = Join-Path $repoRoot 'scripts\run-qemu-x86-qac.ps1'
$batchPath = Join-Path $repoRoot 'scripts\run-qemu-x86-with-build.bat'
$prebuiltRunnerPath = Join-Path $repoRoot 'scripts\run-qemu-x86-prebuilt-qac.ps1'
$prebuiltBatchPath = Join-Path $repoRoot 'scripts\run-qemu-x86.bat'
$moduleText = Get-Content -LiteralPath $modulePath -Raw
$runnerText = Get-Content -LiteralPath $runnerPath -Raw
$batchText = Get-Content -LiteralPath $batchPath -Raw
$prebuiltRunnerText = Get-Content -LiteralPath $prebuiltRunnerPath -Raw
$prebuiltBatchText = Get-Content -LiteralPath $prebuiltBatchPath -Raw

Import-Module $modulePath -Force

$script:checkCount = 0
function Assert-True {
    param([string]$Name, [bool]$Condition)
    $script:checkCount++
    if (-not $Condition) { throw "FAIL [$($script:checkCount)] $Name" }
    Write-Host "PASS [$($script:checkCount)] $Name"
}

function Assert-Equal {
    param([string]$Name, [object]$Expected, [object]$Actual)
    $equal = [object]::Equals($Expected, $Actual)
    Assert-True -Name $Name -Condition $equal
}

$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('guidexos-qac-contract-' + [Guid]::NewGuid().ToString('N'))
$qacPath = Join-Path $fixtureRoot 'QAC tools\qac cli.exe'
$qemuPath = Join-Path $fixtureRoot 'QEMU tools\qemu-system-i386.exe'
$workingDirectory = Join-Path $fixtureRoot 'kernel directory'
$workspaceFixtureRoot = Join-Path $fixtureRoot 'workspace'
$siblingQacPath = Join-Path $fixtureRoot 'guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe'
$defaultKernelFixturePath = Join-Path $workspaceFixtureRoot 'kernel\build\x86\bin\kernel.elf'
$explicitKernelFixturePath = Join-Path $fixtureRoot 'kernel fixture with spaces\existing kernel.elf'
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $qacPath), (Split-Path -Parent $qemuPath), $workingDirectory, $workspaceFixtureRoot, (Split-Path -Parent $siblingQacPath), (Split-Path -Parent $defaultKernelFixturePath), (Split-Path -Parent $explicitKernelFixturePath) | Out-Null
Set-Content -LiteralPath $qacPath -Value 'test fixture' -Encoding ascii
Set-Content -LiteralPath $qemuPath -Value 'test fixture' -Encoding ascii
Set-Content -LiteralPath $siblingQacPath -Value 'test fixture' -Encoding ascii
$elfHeader = [byte[]]::new(128)
$elfHeader[0] = 0x7F; $elfHeader[1] = 0x45; $elfHeader[2] = 0x4C; $elfHeader[3] = 0x46
$elfHeader[4] = 1; $elfHeader[5] = 1; $elfHeader[6] = 1
$elfHeader[16] = 2; $elfHeader[18] = 3; $elfHeader[40] = 52
[IO.File]::WriteAllBytes($defaultKernelFixturePath, $elfHeader)
[IO.File]::WriteAllBytes($explicitKernelFixturePath, $elfHeader)

$originalQacCli = $env:GUIDEXOS_QAC_CLI
try {
    $resolvedExplicit = Resolve-QacCliExecutable -ExplicitPath $qacPath -WorkspaceRoot $repoRoot
    Assert-Equal 'explicit QAC path with spaces resolves exactly' ([IO.Path]::GetFullPath($qacPath)) $resolvedExplicit

    $resolvedSibling = Resolve-QacCliExecutable -WorkspaceRoot $workspaceFixtureRoot
    Assert-Equal 'sibling QAC apphost fallback resolves exactly' ([IO.Path]::GetFullPath($siblingQacPath)) $resolvedSibling

    $resolvedDefaultKernel = Resolve-QacPrebuiltKernelPath -WorkspaceRoot $workspaceFixtureRoot
    Assert-Equal 'prebuilt launcher selects the existing default kernel path' ([IO.Path]::GetFullPath($defaultKernelFixturePath)) $resolvedDefaultKernel
    $resolvedExplicitKernel = Resolve-QacPrebuiltKernelPath -KernelPath $explicitKernelFixturePath -WorkspaceRoot $workspaceFixtureRoot
    Assert-Equal 'explicit prebuilt kernel path with spaces resolves exactly' ([IO.Path]::GetFullPath($explicitKernelFixturePath)) $resolvedExplicitKernel
    $kernelInfo = Get-QacPrebuiltKernelInfo -Path $resolvedExplicitKernel
    Assert-Equal 'existing prebuilt ELF32 i386 kernel passes format validation' 'ELF32 Intel 80386 ET_EXEC' $kernelInfo.Format
    Assert-Equal 'prebuilt kernel size is recorded' 128L $kernelInfo.Size
    $kernelHashBefore = (Get-FileHash -LiteralPath $explicitKernelFixturePath -Algorithm SHA256).Hash
    Assert-Equal 'prebuilt kernel SHA-256 is recorded' $kernelHashBefore $kernelInfo.SHA256
    $kernelHashAfter = (Get-FileHash -LiteralPath $explicitKernelFixturePath -Algorithm SHA256).Hash
    Assert-Equal 'prebuilt artifact remains unchanged by validation' $kernelHashBefore $kernelHashAfter

    $missingKernelRejected = $false
    try {
        [void](Resolve-QacPrebuiltKernelPath -KernelPath (Join-Path $fixtureRoot 'missing kernel.elf') -WorkspaceRoot $workspaceFixtureRoot)
    } catch {
        $missingKernelRejected = $_.Exception.Message.Contains('No build or replacement artifact was generated.')
    }
    Assert-True 'missing prebuilt kernel fails without generating a replacement' $missingKernelRejected

    $invalidKernelPath = Join-Path $fixtureRoot 'kernel fixture with spaces\invalid kernel.elf'
    [IO.File]::WriteAllBytes($invalidKernelPath, [byte[]]::new(128))
    $invalidKernelRejected = $false
    try {
        [void](Get-QacPrebuiltKernelInfo -Path $invalidKernelPath)
    } catch {
        $invalidKernelRejected = $_.Exception.Message.Contains('not a little-endian ELF32 executable')
    }
    Assert-True 'incompatible prebuilt file fails clearly' $invalidKernelRejected

    $missingPathRejected = $false
    try {
        [void](Resolve-QacCliExecutable -ExplicitPath (Join-Path $fixtureRoot 'missing\qac.exe') -WorkspaceRoot $workspaceFixtureRoot)
    } catch {
        $missingPathRejected = $_.Exception.Message.Contains('No unmanaged QEMU launch was attempted.')
    }
    Assert-True 'missing QAC CLI fails without unmanaged fallback' $missingPathRejected

    $env:GUIDEXOS_QAC_CLI = $qacPath
    $resolvedEnvironment = Resolve-QacCliExecutable -WorkspaceRoot $repoRoot
    Assert-Equal 'GUIDEXOS_QAC_CLI is honored' ([IO.Path]::GetFullPath($qacPath)) $resolvedEnvironment

    $edgeArguments = @(
        '-drive', 'file=C:\fixture media\disk image.raw,format=raw',
        '-serial', 'file:C:\fixture logs\serial output.log',
        'embedded"quote',
        '',
        'C:\fixture\trailing\'
    )
    $startInfo = New-QacRunProcessStartInfo `
        -QacCliPath $qacPath `
        -WorkingDirectory $workingDirectory `
        -QemuPath $qemuPath `
        -QemuArguments $edgeArguments
    $arguments = [System.Collections.Generic.List[string]]::new()
    foreach ($item in $startInfo.ArgumentList) { $arguments.Add($item) }

    Assert-Equal 'ProcessStartInfo uses QAC CLI as executable' ([IO.Path]::GetFullPath($qacPath)) $startInfo.FileName
    Assert-Equal 'QAC starts in the original kernel working directory' ([IO.Path]::GetFullPath($workingDirectory)) $startInfo.WorkingDirectory
    Assert-Equal 'QAC process does not invoke a shell' $false $startInfo.UseShellExecute
    Assert-Equal 'QAC run command is first' 'run' $arguments[0]
    Assert-Equal 'QAC project switch is present' '--project' $arguments[1]
    Assert-Equal 'QAC project is guideXOSServer' 'guideXOSServer' $arguments[2]
    Assert-Equal 'QAC mode switch is present' '--mode' $arguments[3]
    Assert-Equal 'QAC mode is development' 'development' $arguments[4]
    Assert-Equal 'QAC command separator is present' '--' $arguments[5]
    Assert-Equal 'QEMU executable path with spaces is preserved' ([IO.Path]::GetFullPath($qemuPath)) $arguments[6]
    Assert-Equal 'argument count is unchanged' (7 + $edgeArguments.Count) $arguments.Count
    Assert-Equal 'disk path boundary with spaces is preserved' $edgeArguments[1] $arguments[8]
    Assert-Equal 'serial path boundary with spaces is preserved' $edgeArguments[3] $arguments[10]
    Assert-Equal 'embedded quote is preserved' $edgeArguments[4] $arguments[11]
    Assert-Equal 'empty argument is preserved' '' $arguments[12]
    Assert-Equal 'trailing backslash is preserved' $edgeArguments[6] $arguments[13]

    $prebuiltArguments = @(New-QacPrebuiltQemuArguments -KernelArgument 'kernel fixture with spaces\existing kernel.elf')
    $expectedPrebuiltArguments = @(
        '-machine', 'pc,usb=off',
        '-kernel', 'kernel fixture with spaces\existing kernel.elf',
        '-m', '128M',
        '-vga', 'std',
        '-serial', 'stdio',
        '-display', 'gtk',
        '-vnc', ':0',
        '-no-reboot',
        '-no-shutdown',
        '-d', 'guest_errors'
    )
    Assert-Equal 'prebuilt launcher preserves the exact original QEMU argument list' ($expectedPrebuiltArguments -join "`0") ($prebuiltArguments -join "`0")
    $prebuiltStartInfo = New-QacRunProcessStartInfo -QacCliPath $qacPath -WorkingDirectory $repoRoot -QemuPath $qemuPath -QemuArguments $prebuiltArguments
    $prebuiltStartArguments = @($prebuiltStartInfo.ArgumentList)
    Assert-Equal 'prebuilt kernel path containing spaces keeps one QEMU argument' 'kernel fixture with spaces\existing kernel.elf' $prebuiltStartArguments[10]
    Assert-Equal 'prebuilt QAC invocation keeps development mode and Server project' 'run|--project|guideXOSServer|--mode|development|--' ($prebuiltStartArguments[0..5] -join '|')
    $qmpArguments = @(New-QacPrebuiltQemuArguments -KernelArgument $resolvedExplicitKernel -QualificationQmpPort 45678)
    Assert-Equal 'test-scoped QMP is loopback-only and not part of normal arguments' 'tcp:127.0.0.1:45678,server=on,wait=off' $qmpArguments[-1]

    $wrapperRejected = $false
    $wrapperPath = Join-Path $fixtureRoot 'wrapper.cmd'
    Set-Content -LiteralPath $wrapperPath -Value 'test fixture' -Encoding ascii
    try {
        [void](New-QacRunProcessStartInfo -QacCliPath $qacPath -WorkingDirectory $workingDirectory -QemuPath $wrapperPath -QemuArguments @())
    } catch {
        $wrapperRejected = $_.Exception.Message.Contains('qemu-system-*.exe')
    }
    Assert-True 'QAC refuses a wrapper as the direct managed child' $wrapperRejected

    $expectedQemuArgs = @(
        '-machine', 'pc,usb=off',
        '-kernel', 'build\x86\bin\kernel.elf',
        '-m', '128M',
        '-vga', 'std',
        '-serial', 'stdio',
        '-display', 'gtk',
        '-vnc', ':0',
        '-no-reboot',
        '-no-shutdown',
        '-d', 'guest_errors'
    )
    foreach ($expectedArgument in $expectedQemuArgs) {
        Assert-True "original QEMU argument preserved: $expectedArgument" ($runnerText.Contains("'$expectedArgument'"))
    }
    Assert-True 'serial output remains attached to stdio' ($runnerText.Contains("'-serial', 'stdio'"))
    Assert-True 'coordinator status check precedes the QAC run process start' ($runnerText.IndexOf('& $qacCli status') -lt $runnerText.IndexOf('[System.Diagnostics.Process]::Start($startInfo)'))
    Assert-True 'coordinator failure exits before launch and reports no fallback' ($runnerText.Contains('QEMU was not launched.') -and $runnerText.Contains('No unmanaged fallback was attempted.'))
    Assert-True 'QAC CLI output provides queue and cancellation status' ($runnerText.Contains('QAC reports queue position and cancellation status.'))
    Assert-True 'the selected launcher returns the QAC child exit code' ($runnerText.Contains('exit $exitCode'))
    Assert-True 'interactive execution waits through QAC child verification' ($runnerText.Contains('$qacProcess.WaitForExit()'))
    Assert-True 'the batch launcher invokes the QAC PowerShell runner' ($batchText.Contains('run-qemu-x86-qac.ps1'))
    Assert-True 'the batch launcher propagates the PowerShell exit code' ($batchText.Contains('exit /b %ERRORLEVEL%'))
    Assert-True 'no direct QEMU spawn remains in the selected launcher' (-not $batchText.Contains('qemu-system-i386.exe') -and -not $runnerText.Contains('Start-Process -FilePath $QemuPath'))
    Assert-True 'no process-name or process-tree cleanup was added' (-not ($moduleText + $runnerText + $batchText | Select-String -Pattern 'Stop-Process|taskkill|Process\.Kill|Kill\(' -Quiet))
    Assert-True 'the interactive launcher adds no QMP or guest timeout policy' (-not $runnerText.Contains("'-qmp'") -and -not $runnerText.Contains('TimeoutSeconds'))
    Assert-True 'the interactive launcher makes no automated guest-marker claim' (-not $runnerText.Contains('result=PASS') -and -not $runnerText.Contains('smoke PASS'))
    Assert-True 'the selected mode cannot be promoted to acceptance' (-not ($moduleText + $runnerText | Select-String -Pattern 'mode.*acceptance' -Quiet))
    Assert-True 'the managed QEMU process is started through QAC' ($runnerText.Contains('[System.Diagnostics.Process]::Start($startInfo)') -and $moduleText.Contains('$startInfo.FileName = (Resolve-Path -LiteralPath $QacCliPath).Path'))

    Assert-True 'prebuilt batch launcher invokes the QAC runner and propagates its exit code' ($prebuiltBatchText.Contains('run-qemu-x86-prebuilt-qac.ps1') -and $prebuiltBatchText.Contains('exit /b %ERRORLEVEL%'))
    Assert-True 'prebuilt launcher does not invoke a build or solution build' (-not ($prebuiltRunnerText + $prebuiltBatchText | Select-String -Pattern 'build-x86\.bat|dotnet\s+build|makefile|make\.exe' -Quiet))
    Assert-True 'prebuilt launcher validates the selected artifact before requesting QAC' ($prebuiltRunnerText.IndexOf('Get-QacPrebuiltKernelInfo') -lt $prebuiltRunnerText.IndexOf('& $qacCli status'))
    Assert-True 'prebuilt launcher records path, size, and prelaunch SHA-256' ($prebuiltRunnerText.Contains('$kernelInfo.Path') -and $prebuiltRunnerText.Contains('$kernelInfo.Size') -and $prebuiltRunnerText.Contains('$kernelInfo.SHA256'))
    Assert-True 'prebuilt launcher checks the artifact hash after the managed run' ($prebuiltRunnerText.Contains('Get-FileHash -LiteralPath $prebuiltKernelPath') -and $prebuiltRunnerText.Contains('Kernel integrity check: unchanged.'))
    Assert-True 'prebuilt launcher uses QAC development mode only' ($prebuiltRunnerText.Contains('project=guideXOSServer mode=development') -and -not ($prebuiltRunnerText | Select-String -Pattern 'mode.*acceptance' -Quiet))
    Assert-True 'prebuilt launcher does not start QEMU when the coordinator is unavailable' ($prebuiltRunnerText.Contains('QAC coordinator unavailable') -and $prebuiltRunnerText.Contains('QEMU was not launched.'))
    Assert-True 'prebuilt launcher retains queue waiting and cancellation behavior' ($prebuiltRunnerText.Contains('Waiting for QAC admission') -and $prebuiltRunnerText.Contains('QAC reports queue position and cancellation status.'))
    Assert-True 'prebuilt launcher tracks and waits for the QAC CLI through verified child exit' ($prebuiltRunnerText.Contains('$qacProcess.WaitForExit()') -and $prebuiltRunnerText.Contains('$qacProcess.ExitCode'))
    Assert-True 'prebuilt launcher propagates zero and nonzero QAC child exit codes' ($prebuiltRunnerText.Contains('$exitCode = $qacProcess.ExitCode') -and $prebuiltRunnerText.Contains('exit $exitCode'))
    Assert-True 'prebuilt launcher reports child-start failure without unmanaged fallback' ($prebuiltRunnerText.Contains('QAC-managed prebuilt QEMU launch failed') -and $prebuiltRunnerText.Contains('No unmanaged fallback was attempted.'))
    Assert-True 'prebuilt launcher performs no process-name or process-tree cleanup' (-not (($moduleText + $prebuiltRunnerText + $prebuiltBatchText) | Select-String -Pattern 'Stop-Process|taskkill|Process\.Kill|Kill\(' -Quiet))
    Assert-True 'normal prebuilt invocation omits QMP and timeout policies' ($prebuiltRunnerText.Contains('QualificationQmpPort') -and $prebuiltRunnerText.Contains('omitted from normal interactive launches') -and -not $prebuiltRunnerText.Contains('TimeoutSeconds'))
    Assert-True 'existing build-and-run launcher remains separate and unchanged' ($batchText.Contains('call build-x86.bat') -and $batchText.Contains('run-qemu-x86-qac.ps1'))
    Assert-True 'prebuilt launcher routes the direct child through QAC rather than direct QEMU' ($prebuiltRunnerText.Contains('[System.Diagnostics.Process]::Start($startInfo)') -and -not $prebuiltRunnerText.Contains('Start-Process -FilePath $QemuPath'))

    Write-Host "QAC development launcher contract checks passed: $($script:checkCount)."
} finally {
    if ($null -eq $originalQacCli) {
        Remove-Item Env:\GUIDEXOS_QAC_CLI -ErrorAction SilentlyContinue
    } else {
        $env:GUIDEXOS_QAC_CLI = $originalQacCli
    }
    Remove-Item -LiteralPath $fixtureRoot -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Module qac-managed-qemu -ErrorAction SilentlyContinue
}
