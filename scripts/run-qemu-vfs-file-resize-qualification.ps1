[CmdletBinding()]
param(
    [int]$TimeoutSeconds = 90
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root

function Find-Tool([string]$Name, [string[]]$Fallbacks) {
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    foreach ($candidate in $Fallbacks) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    throw "$Name was not found."
}

function Assert-NewZeroImage([string]$Path, [long]$ExpectedLength) {
    $item = Get-Item -LiteralPath $Path
    if ($item.Length -ne $ExpectedLength) {
        throw "Disposable image size mismatch: expected $ExpectedLength bytes, found $($item.Length)."
    }

    $hash = [Security.Cryptography.IncrementalHash]::CreateHash(
        [Security.Cryptography.HashAlgorithmName]::SHA256)
    $zeroBlock = New-Object byte[] (1024 * 1024)
    for ($i = 0; $i -lt ($ExpectedLength / $zeroBlock.Length); ++$i) {
        $hash.AppendData($zeroBlock)
    }
    $expectedHash = [BitConverter]::ToString($hash.GetHashAndReset()).Replace("-", "")
    $hash.Dispose()
    $actualHash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    if ($actualHash -ne $expectedHash) {
        throw "Disposable image is not blank; refusing to boot a mutating guest."
    }
    return $actualHash
}

function Invoke-QemuPhase([string]$QemuPath, [string]$LoaderPath,
                          [string]$KernelPath, [string]$DiskPath,
                          [string]$RunDirectory, [string]$Phase,
                          [string]$ExpectedMarker, [int]$WaitSeconds) {
    $serialPath = Join-Path $RunDirectory "$Phase.serial.log"
    $stdoutPath = Join-Path $RunDirectory "$Phase.stdout.log"
    $stderrPath = Join-Path $RunDirectory "$Phase.stderr.log"
    $arguments = @(
        "-machine", "pc,usb=off,accel=tcg", "-m", "1024M",
        "-kernel", $LoaderPath, "-initrd", $KernelPath,
        "-drive", "if=none,id=ndb1r2,file=$DiskPath,format=raw,cache=writeback",
        "-device", "ide-hd,drive=ndb1r2,bus=ide.1,unit=0,serial=GXOSNDB1R2TEST01",
        "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
        "-display", "none", "-serial", "file:$serialPath",
        "-monitor", "none", "-no-reboot"
    )

    $qemuProcess = Start-Process -FilePath $QemuPath -ArgumentList $arguments `
        -PassThru -WindowStyle Hidden -RedirectStandardOutput $stdoutPath `
        -RedirectStandardError $stderrPath
    if (-not $qemuProcess.WaitForExit($WaitSeconds * 1000)) {
        try { $qemuProcess.Kill() } catch {}
        throw "QEMU $Phase phase exceeded its $WaitSeconds second timeout."
    }
    $qemuProcess.WaitForExit()
    $qemuProcess.Refresh()
    $exitCode = $qemuProcess.ExitCode
    if (-not $qemuProcess.HasExited -or
        ($null -ne $exitCode -and $exitCode -ne 33)) {
        $serial = if (Test-Path -LiteralPath $serialPath) {
            Get-Content -LiteralPath $serialPath -Raw
        } else { "<serial log missing>" }
        throw "QEMU $Phase phase returned exit code $exitCode.`n$serial"
    }
    if (-not (Test-Path -LiteralPath $serialPath)) {
        throw "QEMU $Phase phase did not produce a serial log."
    }
    $serial = Get-Content -LiteralPath $serialPath -Raw
    if (-not $serial.Contains($ExpectedMarker) -or
        $serial.Contains("[NDB1R2-VFS-QUAL] FAIL:")) {
        throw "QEMU $Phase phase did not report $ExpectedMarker.`n$serial"
    }
    $exitLabel = if ($null -eq $exitCode) { "process exited" } else { "exit $exitCode" }
    Write-Host "QEMU $Phase phase PASS ($exitLabel); serial: $serialPath"
    return $serialPath
}

$Qemu = Find-Tool "qemu-system-x86_64.exe" @(
    "C:\Program Files\qemu\qemu-system-x86_64.exe",
    "C:\Program Files (x86)\qemu\qemu-system-x86_64.exe",
    "C:\qemu\qemu-system-x86_64.exe")
$QemuImg = Find-Tool "qemu-img.exe" @(
    "C:\Program Files\qemu\qemu-img.exe",
    "C:\Program Files (x86)\qemu\qemu-img.exe",
    "C:\qemu\qemu-img.exe")
$Make = Find-Tool "mingw32-make.exe" @("C:\mingw64\bin\mingw32-make.exe")
$Linker = Find-Tool "ld.lld.exe" @("C:\Program Files\LLVM\bin\ld.lld.exe")
$Nasm = Join-Path $Root "tools\nasm.exe"
if (-not (Test-Path -LiteralPath $Nasm)) { throw "Repository NASM was not found: $Nasm" }

$runName = "NDB1R2_VFS_RESIZE_$(Get-Date -Format 'yyyyMMdd_HHmmss')"
$runDirectory = Join-Path $Root (Join-Path "out\validation" $runName)
if (Test-Path -LiteralPath $runDirectory) {
    throw "Validation directory already exists; refusing to reuse it: $runDirectory"
}
New-Item -ItemType Directory -Path $runDirectory | Out-Null
$diskPath = Join-Path $runDirectory "ndb1r2-disposable-fat32-64MiB.raw"
$loaderObject = Join-Path $runDirectory "qemu_multiboot_loader.o"
$loaderPath = Join-Path $runDirectory "qemu_multiboot_loader.elf"
$kernelPath = Join-Path $Root "kernel\build\amd64-ndb1r2-qemu\bin\kernel.elf"

Write-Host "Building test-only kernel in a separate output directory."
& $Make -C kernel ARCH=amd64 BUILD_DIR=build/amd64-ndb1r2-qemu `
    EXTRA_CFLAGS=-DGXOS_QEMU_VFS_RESIZE_QUALIFICATION -j4
if ($LASTEXITCODE -ne 0) { throw "QEMU qualification kernel build failed." }
if (-not (Test-Path -LiteralPath $kernelPath)) {
    throw "QEMU qualification kernel was not produced: $kernelPath"
}

Write-Host "Building the retained NDB1R1 Multiboot loader."
& $Nasm -f elf32 (Join-Path $Root "tests\ndb1r1_qemu_multiboot_loader.asm") `
    -o $loaderObject
if ($LASTEXITCODE -ne 0) { throw "QEMU Multiboot loader assembly failed." }
& $Linker -m elf_i386 -T `
    (Join-Path $Root "tests\ndb1r1_qemu_multiboot_loader.ld") `
    -o $loaderPath $loaderObject
if ($LASTEXITCODE -ne 0) { throw "QEMU Multiboot loader link failed." }

Write-Host "Creating a new 64 MiB raw image at $diskPath"
& $QemuImg create -f raw $diskPath 64M
if ($LASTEXITCODE -ne 0) { throw "qemu-img failed to create the disposable image." }
$blankImageHash = Assert-NewZeroImage $diskPath 67108864
Write-Host "Verified fresh zero-filled image; SHA-256: $blankImageHash"

$writeLog = Invoke-QemuPhase $Qemu $loaderPath $kernelPath $diskPath `
    $runDirectory "write-phase" "[NDB1R2-VFS-QUAL] WRITE_PHASE PASS" $TimeoutSeconds
$afterWriteHash = (Get-FileHash -LiteralPath $diskPath -Algorithm SHA256).Hash
$readbackLog = Invoke-QemuPhase $Qemu $loaderPath $kernelPath $diskPath `
    $runDirectory "cross-process-readback" `
    "[NDB1R2-VFS-QUAL] CROSS_PROCESS_READBACK PASS" $TimeoutSeconds
$afterReadbackHash = (Get-FileHash -LiteralPath $diskPath -Algorithm SHA256).Hash
if ($afterReadbackHash -ne $afterWriteHash) {
    throw "The independent readback process changed the disposable disk image."
}

Write-Host "Independent QEMU process readback passed; image stayed unchanged."
Write-Host "Validation artifacts: $runDirectory"
Write-Host "Write log: $writeLog"
Write-Host "Readback log: $readbackLog"
