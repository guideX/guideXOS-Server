$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$modulePath = Join-Path $repoRoot 'scripts\qac-managed-qemu.psm1'
$runnerPath = Join-Path $repoRoot 'scripts\run-qemu-x86-qac.ps1'
$batchPath = Join-Path $repoRoot 'scripts\run-qemu-x86-with-build.bat'
$moduleText = Get-Content -LiteralPath $modulePath -Raw
$runnerText = Get-Content -LiteralPath $runnerPath -Raw
$batchText = Get-Content -LiteralPath $batchPath -Raw

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
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $qacPath), (Split-Path -Parent $qemuPath), $workingDirectory, $workspaceFixtureRoot, (Split-Path -Parent $siblingQacPath) | Out-Null
Set-Content -LiteralPath $qacPath -Value 'test fixture' -Encoding ascii
Set-Content -LiteralPath $qemuPath -Value 'test fixture' -Encoding ascii
Set-Content -LiteralPath $siblingQacPath -Value 'test fixture' -Encoding ascii

$originalQacCli = $env:GUIDEXOS_QAC_CLI
try {
    $resolvedExplicit = Resolve-QacCliExecutable -ExplicitPath $qacPath -WorkspaceRoot $repoRoot
    Assert-Equal 'explicit QAC path with spaces resolves exactly' ([IO.Path]::GetFullPath($qacPath)) $resolvedExplicit

    $resolvedSibling = Resolve-QacCliExecutable -WorkspaceRoot $workspaceFixtureRoot
    Assert-Equal 'sibling QAC apphost fallback resolves exactly' ([IO.Path]::GetFullPath($siblingQacPath)) $resolvedSibling

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
