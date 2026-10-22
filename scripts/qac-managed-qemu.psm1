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

function Resolve-QacPrebuiltKernelPath {
    [CmdletBinding()]
    param(
        [string]$KernelPath,
        [Parameter(Mandatory = $true)]
        [string]$WorkspaceRoot
    )

    if ([string]::IsNullOrWhiteSpace($KernelPath)) {
        $candidate = Join-Path $WorkspaceRoot 'kernel\build\x86\bin\kernel.elf'
    } elseif ([IO.Path]::IsPathRooted($KernelPath)) {
        $candidate = $KernelPath
    } else {
        $candidate = Join-Path $WorkspaceRoot $KernelPath
    }

    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw "Prebuilt x86 kernel not found at '$candidate'. No build or replacement artifact was generated."
    }

    return (Resolve-Path -LiteralPath $candidate).Path
}

function Get-QacPrebuiltKernelInfo {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $resolvedPath = (Resolve-Path -LiteralPath $Path -ErrorAction Stop).Path
    $stream = $null
    try {
        $stream = [IO.File]::Open($resolvedPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        if ($stream.Length -lt 52) {
            throw 'ELF32 executable header is incomplete.'
        }

        $header = [byte[]]::new(52)
        $read = $stream.Read($header, 0, $header.Length)
        if ($read -ne $header.Length) {
            throw 'ELF32 executable header could not be read completely.'
        }
        if ($header[0] -ne 0x7F -or $header[1] -ne 0x45 -or $header[2] -ne 0x4C -or $header[3] -ne 0x46 -or
            $header[4] -ne 1 -or $header[5] -ne 1 -or $header[6] -ne 1) {
            throw 'The selected file is not a little-endian ELF32 executable.'
        }

        $objectType = [int]$header[16] -bor ([int]$header[17] -shl 8)
        $machine = [int]$header[18] -bor ([int]$header[19] -shl 8)
        $headerSize = [int]$header[40] -bor ([int]$header[41] -shl 8)
        if ($objectType -ne 2 -or $machine -ne 3 -or $headerSize -ne 52) {
            throw "The selected file is not an ELF32 Intel 80386 executable (type=$objectType, machine=$machine, header-size=$headerSize)."
        }

        $size = $stream.Length
    } catch {
        throw "Prebuilt kernel '$resolvedPath' is unreadable or incompatible with qemu-system-i386: $($_.Exception.Message)"
    } finally {
        if ($null -ne $stream) {
            $stream.Dispose()
        }
    }

    $hash = (Get-FileHash -LiteralPath $resolvedPath -Algorithm SHA256 -ErrorAction Stop).Hash
    return [pscustomobject]@{
        Path   = $resolvedPath
        Size   = $size
        SHA256 = $hash
        Format = 'ELF32 Intel 80386 ET_EXEC'
    }
}

function New-QacPrebuiltQemuArguments {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$KernelArgument,
        [ValidateRange(1024, 65535)]
        [Nullable[int]]$QualificationQmpPort
    )

    $arguments = @(
        '-machine', 'pc,usb=off',
        '-kernel', $KernelArgument,
        '-m', '128M',
        '-vga', 'std',
        '-serial', 'stdio',
        '-display', 'gtk',
        '-vnc', ':0',
        '-no-reboot',
        '-no-shutdown',
        '-d', 'guest_errors'
    )

    if ($null -ne $QualificationQmpPort) {
        $arguments += @('-qmp', "tcp:127.0.0.1:$QualificationQmpPort,server=on,wait=off")
    }

    return $arguments
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

Export-ModuleMember -Function Resolve-QacCliExecutable, Resolve-QacPrebuiltKernelPath, Get-QacPrebuiltKernelInfo, New-QacPrebuiltQemuArguments, New-QacRunProcessStartInfo
