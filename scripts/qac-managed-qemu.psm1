Set-StrictMode -Version Latest

function Resolve-QacCliExecutable {
    [CmdletBinding()]
    param(
        [string]$ExplicitPath,
        [Parameter(Mandatory = $true)]
        [string]$WorkspaceRoot
    )

    $candidate = $null
    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        $candidate = $ExplicitPath
    } elseif (-not [string]::IsNullOrWhiteSpace($env:GUIDEXOS_QAC_CLI)) {
        $candidate = $env:GUIDEXOS_QAC_CLI
    } else {
        $command = Get-Command -Name 'qac.exe' -CommandType Application -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($command) {
            $candidate = $command.Source
        } else {
            $candidate = Join-Path $WorkspaceRoot '..\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe'
        }
    }

    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw "QAC CLI is unavailable at '$candidate'. Set GUIDEXOS_QAC_CLI or pass -QacCliPath. No unmanaged QEMU launch was attempted."
    }
    if ([IO.Path]::GetExtension($candidate) -ne '.exe') {
        throw "QAC CLI must be a Windows executable: '$candidate'. No unmanaged QEMU launch was attempted."
    }

    return (Resolve-Path -LiteralPath $candidate).Path
}

function New-QacRunProcessStartInfo {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$QacCliPath,
        [Parameter(Mandatory = $true)]
        [string]$WorkingDirectory,
        [Parameter(Mandatory = $true)]
        [string]$QemuPath,
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [AllowEmptyString()]
        [string[]]$QemuArguments
    )

    if (-not (Test-Path -LiteralPath $QacCliPath -PathType Leaf)) {
        throw "QAC CLI is unavailable: '$QacCliPath'. No unmanaged QEMU launch was attempted."
    }
    if (-not (Test-Path -LiteralPath $WorkingDirectory -PathType Container)) {
        throw "QAC working directory does not exist: '$WorkingDirectory'."
    }
    if ([IO.Path]::GetFileName($QemuPath) -notmatch '^qemu-system-.+\.exe$' -or
        -not (Test-Path -LiteralPath $QemuPath -PathType Leaf)) {
        throw "QEMU executable is missing or is not a qemu-system-*.exe image: '$QemuPath'."
    }

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = (Resolve-Path -LiteralPath $QacCliPath).Path
    $startInfo.WorkingDirectory = (Resolve-Path -LiteralPath $WorkingDirectory).Path
    $startInfo.UseShellExecute = $false

    foreach ($argument in @('run', '--project', 'guideXOSServer', '--mode', 'development', '--', $QemuPath) + $QemuArguments) {
        if ($null -eq $argument) {
            throw 'QEMU arguments cannot contain null entries.'
        }
        [void]$startInfo.ArgumentList.Add([string]$argument)
    }

    return $startInfo
}

Export-ModuleMember -Function Resolve-QacCliExecutable, New-QacRunProcessStartInfo
