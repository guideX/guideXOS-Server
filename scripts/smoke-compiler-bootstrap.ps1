[CmdletBinding()]
param(
    [int]$BootCount = 3,
    [int]$TimeoutSeconds = 45,
    [switch]$Phase27E
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$kernelDirectory = Join-Path $root "kernel"
$espDirectory = Join-Path $root "ESP"
$fixtureDirectory = Join-Path $root "scripts/fixtures/phase27b"
$phase27dFixtureDirectory = Join-Path $root "scripts/fixtures/phase27d"
$phase27eFixtureDirectory = Join-Path $root "scripts/fixtures/phase27e"
$developerStudioRoot = Join-Path (Split-Path -Parent $root) "guideXOS_Developer_Studio"
$phase27eAppDirectory = Join-Path $root "Apps/DS27E"
$qemuPath = "C:\Program Files\qemu\qemu-system-x86_64.exe"
$ovmfCodePath = Join-Path $root "OVMF.fd"
$tempDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ("guidexos-phase27d-" + [guid]::NewGuid().ToString("N"))
$evidenceDirectory = Join-Path $tempDirectory "artifacts"
$backups = @{}
$directoryBackups = @{}
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

function Export-SerialArtifact([string]$serial, [string]$name, [string]$destination) {
    $escapedName = [regex]::Escape($name)
    $pattern = "(?s)NativeElf: artifact_begin=$escapedName bytes=([0-9A-Fa-f]{8})\r?\nNativeElf: artifact_hex=([0-9A-Fa-f]+)\r?\nNativeElf: artifact_end=$escapedName"
    $match = [regex]::Match($serial, $pattern)
    if (!$match.Success) { throw "serial ELF evidence missing: $name" }

    $byteCount = [Convert]::ToInt32($match.Groups[1].Value, 16)
    $hex = $match.Groups[2].Value
    if ($byteCount -le 0 -or $hex.Length -ne ($byteCount * 2)) {
        throw "serial ELF evidence has invalid length: $name"
    }

    $bytes = New-Object byte[] $byteCount
    for ($index = 0; $index -lt $byteCount; ++$index) {
        $bytes[$index] = [Convert]::ToByte($hex.Substring($index * 2, 2), 16)
    }
    [System.IO.File]::WriteAllBytes($destination, $bytes)
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
            "ELF Loader: Phase 27C smoke PASS",
            "NativeElf host log: Hello from guideXOS!",
            "NativeElf host log: Developer Studio native build works!",
            "phase27d_dedicated_stack=PASS",
            "phase27d_app_context=PASS",
            "phase27d_host_log=PASS",
            "phase27d_source_driven_host_call=PASS",
            "phase27d_return_value=PASS",
            "phase27d_repeat_lifecycle=PASS",
            "phase27d_host_call_validation=PASS",
            "phase27d_kernel_survival=PASS",
            "phase27d=PASS",
            "ELF Loader: Phase 27D smoke PASS"
        )
        if ($Phase27E) {
            $requiredMarkers += @(
                "phase27e_build_backend=PASS",
                "phase27e_ide_build=PASS",
                "phase27e_source_edit_build=PASS",
                "phase27e_ide_diagnostics=PASS",
                "phase27e_rebuild_after_failure=PASS",
                "phase27e_kernel_survival=PASS",
                "phase27e=PASS",
                "phase27e_app_launch=PASS",
                "ELF Loader: Phase 27E smoke PASS"
            )
        }
        $missingMarkers = @($requiredMarkers | Where-Object { $serial -notmatch [regex]::Escape($_) })
        if ($missingMarkers.Count -ne 0) {
            Write-Host "QEMU boot $runNumber missed required Phase 27B/27C/27D markers: $($missingMarkers -join ', ')" -ForegroundColor Red
            if ($Phase27E) {
                $serial -split "`r?`n" | Where-Object { $_ -match "phase27e|Phase 27E" } | ForEach-Object { Write-Host $_ }
            }
            if ($serial) { Write-Host $serial }
            if ($stderr) { Write-Host $stderr }
            throw "Phase 27B/27C/27D QEMU proof failed on boot $runNumber (exit $($process.ExitCode))"
        }

        Write-Host "--- QEMU bare-metal compiler proof boot $runNumber ---" -ForegroundColor Cyan
        $serial -split "`r?`n" |
            Where-Object { $_ -notmatch "NativeElf: artifact_hex=" -and
                           $_ -match "Compiler:|ELF Loader:|NativeElf:|phase27c|phase27d|phase27e|^error:" } |
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
    if ($Phase27E) {
        if (!(Test-Path (Join-Path $developerStudioRoot "scripts/build-phase27e.ps1"))) {
            throw "Developer Studio Phase 27E build script is missing: $developerStudioRoot"
        }
        & powershell -ExecutionPolicy Bypass -File (Join-Path $developerStudioRoot "scripts/build-phase27e.ps1") -ServerRoot $root
        if ($LASTEXITCODE -ne 0) { throw "Developer Studio Phase 27E proof app build failed" }
    }
    $env:EXTRA_CFLAGS = "-DGXOS_COMPILER_BOOTSTRAP_SMOKE_ACTIVE"
    if ($Phase27E) { $env:EXTRA_CFLAGS += " -DGXOS_PHASE27E_SMOKE" }
    Push-Location $kernelDirectory
    try {
        Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $kernelDirectory "build/amd64/obj/core/main.o")
        if ($Phase27E) {
            Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $kernelDirectory "build/amd64/obj/core/native_elf/native_elf_smoke.o")
        }
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
        "d27a.c", "d27b.c", "d27c.c", "d27a.elf", "d27b.elf", "d27c.elf",
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
    if ($Phase27E) {
        foreach ($relativeDirectory in @("P27E", "Apps/DS27E")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path $target -PathType Leaf) { throw "ESP target is a file: $target" }
            if (Test-Path $target -PathType Container) {
                $backup = Join-Path $tempDirectory ("backup-directory-" + ($relativeDirectory -replace '[/\\]', '-'))
                Copy-Item $target $backup -Recurse -Force
                $directoryBackups[$relativeDirectory] = $backup
            }
        }
    }
    New-Item -ItemType Directory -Force -Path (Join-Path $espDirectory "EFI/BOOT") | Out-Null
    Copy-Item (Join-Path $fixtureDirectory "r42.c") (Join-Path $espDirectory "r42.c") -Force
    Copy-Item (Join-Path $fixtureDirectory "r41.c") (Join-Path $espDirectory "r41.c") -Force
    Copy-Item (Join-Path $fixtureDirectory "bad.c") (Join-Path $espDirectory "bad.c") -Force
    Copy-Item (Join-Path $phase27dFixtureDirectory "d27a.c") (Join-Path $espDirectory "d27a.c") -Force
    Copy-Item (Join-Path $phase27dFixtureDirectory "d27b.c") (Join-Path $espDirectory "d27b.c") -Force
    Copy-Item (Join-Path $phase27dFixtureDirectory "d27c.c") (Join-Path $espDirectory "d27c.c") -Force
    Copy-Item $kernelBinary (Join-Path $espDirectory "kernel.elf") -Force
    Copy-Item $bootloaderBinary (Join-Path $espDirectory "EFI/BOOT/BOOTX64.EFI") -Force
    if ($Phase27E) {
        Copy-Item $phase27eFixtureDirectory (Join-Path $espDirectory "P27E") -Recurse -Force
        New-Item -ItemType Directory -Force -Path (Join-Path $espDirectory "Apps") | Out-Null
        Copy-Item $phase27eAppDirectory (Join-Path $espDirectory "Apps/DS27E") -Recurse -Force
        if (!(Test-Path -LiteralPath (Join-Path $espDirectory "P27E/guidexos.project") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27E/src/main.cpp") -PathType Leaf)) {
            throw "Phase 27E project fixture was not staged into ESP"
        }
    }

    # Each boot receives a clean guest output namespace. The compiler itself
    # creates/replaces these files through guideXOS VFS path-level operations.
    foreach ($relativePath in @(
        "r42.elf", "r42b.elf", "r41.elf", "bad.elf",
        "p27magic.elf", "p27arch.elf", "p27entry.elf", "p27out.elf", "p27trunc.elf",
        "p27bnd.elf", "p27addr.elf", "d27a.elf", "d27b.elf", "d27c.elf")) {
        $target = Join-Path $espDirectory $relativePath
        if (Test-Path $target) { Remove-Item -LiteralPath $target -Force }
    }

    for ($run = 1; $run -le $BootCount; ++$run) {
        if ($run -gt 1) {
            foreach ($relativePath in @(
                "r42.elf", "r42b.elf", "r41.elf", "bad.elf",
                "p27magic.elf", "p27arch.elf", "p27entry.elf", "p27out.elf", "p27trunc.elf",
                "p27bnd.elf", "p27addr.elf", "d27a.elf", "d27b.elf", "d27c.elf")) {
                $target = Join-Path $espDirectory $relativePath
                if (Test-Path $target) { Remove-Item -LiteralPath $target -Force }
            }
        }
        if ($Phase27E -and $run -gt 1) {
            $projectTarget = Join-Path $espDirectory "P27E"
            if (Test-Path $projectTarget) { Remove-Item -LiteralPath $projectTarget -Recurse -Force }
            Copy-Item $phase27eFixtureDirectory $projectTarget -Recurse -Force
        }
        Invoke-QemuProofBoot $run $qemu
    }

    # The guest compiler writes its artifacts through the boot-time VFS.  The
    # default smoke image is memory-backed during a boot, so the guest emits
    # exact generated bytes over serial for independent host-side inspection.
    $finalSerial = Read-SerialText (Join-Path $tempDirectory ("boot{0}.serial.log" -f $BootCount))
    foreach ($artifact in @("r42", "d27a", "d27b", "d27c")) {
        Export-SerialArtifact $finalSerial $artifact (Join-Path $evidenceDirectory ($artifact + ".elf"))
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
    Write-Host "--- external audit of guest-generated d27a.elf ---" -ForegroundColor Cyan
    & $readelf -h -l (Join-Path $evidenceDirectory "d27a.elf")
    & $objdump -D -Mintel -b binary -m i386:x86-64 --adjust-vma=0x10000000 `
        --start-address=0x10001000 --stop-address=0x10001022 (Join-Path $evidenceDirectory "d27a.elf")
    if ($LASTEXITCODE -ne 0) { throw "external Phase 27D ELF inspection failed" }
    if ($Phase27E) {
        Write-Host "Phase 27B/27C/27D/27E QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
    } else {
        Write-Host "Phase 27B/27C/27D QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
    }
}
finally {
    if ($null -eq $oldExtraCFlags) { Remove-Item Env:EXTRA_CFLAGS -ErrorAction SilentlyContinue }
    else { $env:EXTRA_CFLAGS = $oldExtraCFlags }

    # QEMU may release the FAT image handles just after it exits. Retry the
    # exact generated paths so a proof run never leaves build artifacts in ESP.
    Start-Sleep -Milliseconds 250
    foreach ($relativePath in @(
        "r42.c", "r41.c", "bad.c", "d27a.c", "d27b.c", "d27c.c", "r42.elf", "r42b.elf", "r41.elf", "bad.elf", "d27a.elf", "d27b.elf", "d27c.elf",
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
    if ($Phase27E) {
        foreach ($relativeDirectory in @("P27E", "Apps/DS27E")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path -LiteralPath $target -PathType Container) {
                Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
            }
            if ($directoryBackups.ContainsKey($relativeDirectory)) {
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
                Copy-Item $directoryBackups[$relativeDirectory] $target -Recurse -Force
            }
        }
    }
    if (Test-Path $tempDirectory) { Remove-Item -LiteralPath $tempDirectory -Recurse -Force -ErrorAction SilentlyContinue }
}
