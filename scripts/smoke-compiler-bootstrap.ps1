[CmdletBinding()]
param(
    [int]$BootCount = 3,
    [int]$TimeoutSeconds = 45
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$kernelDirectory = Join-Path $root "kernel"
$espDirectory = Join-Path $root "ESP"
$fixtureDirectory = Join-Path $root "scripts/fixtures/phase27b"
$qemuPath = "C:\Program Files\qemu\qemu-system-x86_64.exe"
$ovmfCodePath = Join-Path $root "OVMF.fd"
$tempDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ("guidexos-phase27c-" + [guid]::NewGuid().ToString("N"))
$evidenceDirectory = Join-Path $tempDirectory "artifacts"
$backups = @{}
$oldExtraCFlags = $env:EXTRA_CFLAGS

function Get-RequiredTool([string]$name, [string]$fallback) {
    $command = Get-Command $name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    if ($fallback -and (Test-Path $fallback)) { return $fallback }
    throw "Required tool not found: $name"
}

function Quote-ProcessArgument([string]$value) {
    if ($value -notmatch '[\s"]') { return $value }
    return '"' + $value.Replace('"', '\"') + '"'
}

function Read-SerialText([string]$path) {
    if (!(Test-Path $path)) { return "" }
    return [System.IO.File]::ReadAllText($path)
}

function Invoke-QemuProofBoot([int]$runNumber, [string]$qemu) {
    $serialPath = Join-Path $tempDirectory ("boot{0}.serial.log" -f $runNumber)
    $stderrPath = Join-Path $tempDirectory ("boot{0}.stderr.log" -f $runNumber)
    $qemuArguments = @(
        "-machine", "pc,usb=off",
        "-drive", "if=pflash,format=raw,readonly=on,file=$ovmfCodePath",
        "-drive", "file=fat:rw:$espDirectory,format=raw,if=ide,index=0",
        "-m", "1024M",
        "-vga", "std",
        "-serial", "file:$serialPath",
        "-display", "none",
        "-no-reboot",
        "-no-shutdown"
    )

    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $qemu
    $startInfo.Arguments = (($qemuArguments | ForEach-Object { Quote-ProcessArgument $_ }) -join " ")
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $startInfo
    if (!$process.Start()) { throw "QEMU did not start for boot $runNumber" }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)

    try {
        while (!$process.HasExited -and [DateTime]::UtcNow -lt $deadline) {
            Start-Sleep -Milliseconds 250
        }

        if (!$process.HasExited) {
            Start-Sleep -Milliseconds 750
            if (!$process.HasExited) { $process.Kill() }
        }
        $process.WaitForExit()
        $stdout = $stdoutTask.GetAwaiter().GetResult()
        $stderr = $stderrTask.GetAwaiter().GetResult()
        $serial = Read-SerialText $serialPath
        if ($stderr) { [System.IO.File]::WriteAllText($stderrPath, $stderr) }

        $requiredMarkers = @(
            "Compiler: Phase 27B smoke PASS",
            "phase27c_compile42=PASS",
            "phase27c_execute42=PASS",
            "phase27c_compile41=PASS",
            "phase27c_execute41=PASS",
            "phase27c_repeat_execution=PASS",
            "phase27c_invalid_elf=PASS",
            "phase27c_alternate_build_run=PASS",
            "phase27c_kernel_survival=PASS",
            "phase27c=PASS",
            "ELF Loader: Phase 27C smoke PASS"
        )
        $missingMarkers = @($requiredMarkers | Where-Object { $serial -notmatch [regex]::Escape($_) })
        if ($missingMarkers.Count -ne 0) {
            Write-Host "QEMU boot $runNumber missed required Phase 27C markers: $($missingMarkers -join ', ')" -ForegroundColor Red
            if ($serial) { Write-Host $serial }
            if ($stderr) { Write-Host $stderr }
            throw "Phase 27B QEMU proof failed on boot $runNumber (exit $($process.ExitCode))"
        }

        Write-Host "--- QEMU bare-metal compiler proof boot $runNumber ---" -ForegroundColor Cyan
        $serial -split "`r?`n" |
            Where-Object { $_ -match "Compiler:|ELF Loader:|phase27c|^error:" } |
            ForEach-Object { Write-Host $_ }
    }
    finally {
        if (!$process.HasExited) { $process.Kill() }
        $process.Dispose()
    }
}

try {
    if ($BootCount -lt 1) { throw "BootCount must be at least 1" }
    if (!(Test-Path $espDirectory)) { throw "ESP directory is missing: $espDirectory" }
    if (!(Test-Path $ovmfCodePath)) { throw "Repository OVMF firmware is missing" }
    New-Item -ItemType Directory -Force -Path $tempDirectory, $evidenceDirectory | Out-Null
    $qemu = Get-RequiredTool "qemu-system-x86_64" $qemuPath

    $makeFallback = "C:\mingw64\bin\mingw32-make.exe"
    $make = Get-RequiredTool "mingw32-make" $makeFallback
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (!(Test-Path $vswhere)) { throw "Visual Studio vswhere.exe is missing" }
    $visualStudioPath = (& $vswhere -latest -property installationPath | Select-Object -First 1).Trim()
    $msbuild = Join-Path $visualStudioPath "MSBuild\Current\Bin\MSBuild.exe"
    if (!(Test-Path $msbuild)) { throw "MSBuild is missing: $msbuild" }

    # The host toolchain only builds the kernel/bootloader test harness. It is
    # never called by the guest compiler while it reads and emits the ELF.
    $env:EXTRA_CFLAGS = "-DGXOS_COMPILER_BOOTSTRAP_SMOKE_ACTIVE"
    Push-Location $kernelDirectory
    try {
        Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $kernelDirectory "build/amd64/obj/core/main.o")
        & $make ARCH=amd64 "EXTRA_CFLAGS=$env:EXTRA_CFLAGS" "MBEDTLS_GUIDEXOS_IMPORT_STATE_DEPS="
        if ($LASTEXITCODE -ne 0) { throw "kernel build failed" }
    }
    finally {
        Pop-Location
    }

    & $msbuild (Join-Path $root "guideXOSBootLoader/guideXOSBootLoader.vcxproj") `
        /p:Configuration=Release /p:Platform=x64 /t:Rebuild /m /nologo /verbosity:minimal
    if ($LASTEXITCODE -ne 0) { throw "bootloader build failed" }

    $kernelBinary = Join-Path $kernelDirectory "build/amd64/bin/kernel.elf"
    $bootloaderBinary = Join-Path $root "guideXOSBootLoader/x64/Release/guideXOSBootLoader.exe"
    if (!(Test-Path $kernelBinary) -or !(Test-Path $bootloaderBinary)) {
        throw "fresh kernel or bootloader output is missing"
    }

    $managedFiles = @(
        "r42.c", "r41.c", "bad.c", "r42.elf", "r42b.elf", "r41.elf", "bad.elf",
        "p27magic.elf", "p27arch.elf", "p27entry.elf", "p27out.elf", "p27trunc.elf",
        "p27bnd.elf", "p27addr.elf",
        "kernel.elf", "EFI/BOOT/BOOTX64.EFI", "NvVars"
    )
    foreach ($relativePath in $managedFiles) {
        $target = Join-Path $espDirectory $relativePath
        if (Test-Path $target -PathType Container) { throw "ESP target is a directory: $target" }
        if (Test-Path $target) {
            $backup = Join-Path $tempDirectory ("backup-" + ($relativePath -replace '[/\\]', '-'))
            Copy-Item $target $backup -Force
            $backups[$relativePath] = $backup
        }
    }
    New-Item -ItemType Directory -Force -Path (Join-Path $espDirectory "EFI/BOOT") | Out-Null
    Copy-Item (Join-Path $fixtureDirectory "r42.c") (Join-Path $espDirectory "r42.c") -Force
    Copy-Item (Join-Path $fixtureDirectory "r41.c") (Join-Path $espDirectory "r41.c") -Force
    Copy-Item (Join-Path $fixtureDirectory "bad.c") (Join-Path $espDirectory "bad.c") -Force
    Copy-Item $kernelBinary (Join-Path $espDirectory "kernel.elf") -Force
    Copy-Item $bootloaderBinary (Join-Path $espDirectory "EFI/BOOT/BOOTX64.EFI") -Force

    # Each boot receives a clean guest output namespace. The compiler itself
    # creates/replaces these files through guideXOS VFS path-level operations.
    foreach ($relativePath in @(
        "r42.elf", "r42b.elf", "r41.elf", "bad.elf",
        "p27magic.elf", "p27arch.elf", "p27entry.elf", "p27out.elf", "p27trunc.elf",
        "p27bnd.elf", "p27addr.elf")) {
        $target = Join-Path $espDirectory $relativePath
        if (Test-Path $target) { Remove-Item -LiteralPath $target -Force }
    }

    for ($run = 1; $run -le $BootCount; ++$run) {
        if ($run -gt 1) {
            foreach ($relativePath in @(
                "r42.elf", "r42b.elf", "r41.elf", "bad.elf",
                "p27magic.elf", "p27arch.elf", "p27entry.elf", "p27out.elf", "p27trunc.elf",
                "p27bnd.elf", "p27addr.elf")) {
                $target = Join-Path $espDirectory $relativePath
                if (Test-Path $target) { Remove-Item -LiteralPath $target -Force }
            }
        }
        Invoke-QemuProofBoot $run $qemu
    }

    foreach ($artifact in @("r42.elf", "r41.elf")) {
        $sourceArtifact = Join-Path $espDirectory $artifact
        if (!(Test-Path $sourceArtifact)) { throw "guest artifact missing after proof: $sourceArtifact" }
        Copy-Item $sourceArtifact (Join-Path $evidenceDirectory $artifact) -Force
    }

    $readelf = Get-RequiredTool "readelf" ""
    $objdump = Get-RequiredTool "objdump" ""
    Write-Host "--- external audit of guest-generated r42.elf ---" -ForegroundColor Cyan
    & $readelf -h -l (Join-Path $evidenceDirectory "r42.elf")
    # The bootstrap intentionally omits section metadata. Ask objdump to audit
    # the ELF's known file-backed code range as a raw AMD64 view.
    & $objdump -D -Mintel -b binary -m i386:x86-64 --adjust-vma=0x10000000 `
        --start-address=0x10001000 --stop-address=0x10001006 (Join-Path $evidenceDirectory "r42.elf")
    if ($LASTEXITCODE -ne 0) { throw "external ELF inspection failed" }
    Write-Host "Phase 27C QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
}
finally {
    if ($null -eq $oldExtraCFlags) { Remove-Item Env:EXTRA_CFLAGS -ErrorAction SilentlyContinue }
    else { $env:EXTRA_CFLAGS = $oldExtraCFlags }

    # QEMU may release the FAT image handles just after it exits. Retry the
    # exact generated paths so a proof run never leaves build artifacts in ESP.
    Start-Sleep -Milliseconds 250
    foreach ($relativePath in @(
        "r42.c", "r41.c", "bad.c", "r42.elf", "r42b.elf", "r41.elf", "bad.elf",
        "p27magic.elf", "p27arch.elf", "p27entry.elf", "p27out.elf", "p27trunc.elf",
        "p27bnd.elf", "p27addr.elf",
        "kernel.elf", "EFI/BOOT/BOOTX64.EFI", "NvVars")) {
        $target = Join-Path $espDirectory $relativePath
        for ($attempt = 0; $attempt -lt 5 -and (Test-Path -LiteralPath $target); ++$attempt) {
            Remove-Item -LiteralPath $target -Force -ErrorAction SilentlyContinue
            if (Test-Path -LiteralPath $target) { Start-Sleep -Milliseconds 100 }
        }
        if ($backups.ContainsKey($relativePath)) {
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
            Copy-Item $backups[$relativePath] $target -Force
        }
    }
    if (Test-Path $tempDirectory) { Remove-Item -LiteralPath $tempDirectory -Recurse -Force -ErrorAction SilentlyContinue }
}
