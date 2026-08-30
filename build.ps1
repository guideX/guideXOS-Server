#
# guideXOS Complete Build Script
#
# Builds: Bootloader + Kernel + Sets up ESP
# Usage: .\build-uefi.ps1 [-Clean] [-SkipKernel] [-RunQemu]
#
# Copyright (c) 2024 guideX
#

param(
    [switch]$Clean,
    [switch]$SkipKernel,
    [switch]$RunQemu,
    [switch]$FsTest,
    [switch]$Fat32Only,
    [switch]$Ext4Only,
    [switch]$Debug,
    [switch]$WaitGdb,
    [string]$Memory = "1024M",
    [string]$Arch = "amd64",
    [ValidateRange(0, 8)]
    [int]$I219Phase5Stage = 8,
    [ValidateRange(0, 6)]
    [int]$I219Phase6Stage = 0
)

$ErrorActionPreference = "Stop"

Write-Host "====================================" -ForegroundColor Cyan
Write-Host "  guideXOS Complete Build System" -ForegroundColor Cyan
Write-Host "====================================" -ForegroundColor Cyan
Write-Host "  Build identity: GXOS-LARGE-FILE-PASTE-TRACE-V1" -ForegroundColor Cyan
Write-Host "  Build probe ID: GXOS-LFPASTE-20260726-02" -ForegroundColor Cyan
Write-Host "  I219 Phase 5 stage: $I219Phase5Stage" -ForegroundColor Cyan
Write-Host "  I219 Phase 6 micro-stage: $I219Phase6Stage" -ForegroundColor Cyan
Write-Host ""

$RootDir = $PSScriptRoot
$ProcessEnvironmentScript = Join-Path $RootDir "scripts\process_environment.ps1"
. $ProcessEnvironmentScript
Normalize-ProcessEnvironment
. (Join-Path $RootDir "scripts\qemu-secure-rng-args.ps1")
. (Join-Path $RootDir "scripts\build-native-command.ps1")
$ESPDir = Join-Path $RootDir "ESP"
$KernelDir = Join-Path $RootDir "kernel"
$BootloaderDir = Join-Path $RootDir "guideXOSBootLoader"
$DiskDir = Join-Path $RootDir "disks"
$KernelBuildSkipped = $false
$WallpaperRuntimeImageBuilt = $false
$SkipWallpaperRuntimeImageBuild = $env:GXOS_SKIP_WALLPAPER_RUNTIME_IMAGE_BUILD -eq "1"

function Build-WallpaperRuntimeImage {
    if ($script:WallpaperRuntimeImageBuilt) {
        return
    }

    if ($SkipWallpaperRuntimeImageBuild) {
        $Ramdisk = Join-Path $ESPDir "ramdisk.img"
        if (Test-Path $Ramdisk) {
            Write-Host "      Reusing existing wallpaper runtime image..." -ForegroundColor DarkGray
            $script:WallpaperRuntimeImageBuilt = $true
            return
        }
    }

    if (!(Test-Path $ESPDir)) {
        New-Item -ItemType Directory -Path $ESPDir -Force | Out-Null
    }

    $Ramdisk = Join-Path $ESPDir "ramdisk.img"
    $WallpaperPackScript = Join-Path $RootDir "scripts\generate-wallpaper-pack.ps1"
    if (Test-Path $WallpaperPackScript) {
        Write-Host "      Building wallpaper filesystem image..." -ForegroundColor Cyan
        $smokeCaFixture = $env:GXOS_NAVIGATOR_SMOKE_CA_FIXTURE -eq "1"
        if ($smokeCaFixture) {
            & $WallpaperPackScript -InputDir (Join-Path $RootDir "assets\Backgrounds") -OutputDir (Join-Path $RootDir "out\wallpaper-pack") -OutputImage $Ramdisk -NativePackageDirs @((Join-Path $RootDir "Apps\PacMan"), (Join-Path $RootDir "Apps\DeveloperStudio")) -SmokeCaFixture
        } else {
            & $WallpaperPackScript -InputDir (Join-Path $RootDir "assets\Backgrounds") -OutputDir (Join-Path $RootDir "out\wallpaper-pack") -OutputImage $Ramdisk -NativePackageDirs @((Join-Path $RootDir "Apps\PacMan"), (Join-Path $RootDir "Apps\DeveloperStudio"))
        }
        $script:WallpaperRuntimeImageBuilt = $true
    } elseif (!(Test-Path $Ramdisk)) {
        $emptyRamdisk = New-Object byte[] 1048576
        [System.IO.File]::WriteAllBytes($Ramdisk, $emptyRamdisk)
        Write-Host "      WARNING: Wallpaper pack script missing; created empty ramdisk.img" -ForegroundColor Yellow
        $script:WallpaperRuntimeImageBuilt = $true
    }
}

function Build-PacmanPackage {
    if ($Arch -ne "amd64") {
        Write-Host "      PacMan package build skipped for unsupported architecture: $Arch" -ForegroundColor Yellow
        return
    }

    $pacmanBuildScript = Join-Path $RootDir "scripts\build-pacman-package.ps1"
    if (!(Test-Path -LiteralPath $pacmanBuildScript -PathType Leaf)) {
        Write-Host "      ERROR: PacMan build script not found at: $pacmanBuildScript" -ForegroundColor Red
        exit 1
    }

    $pacmanBuildArgs = @{
        OutputPackage = (Join-Path $RootDir "Apps\PacMan")
        Clean = $Clean
    }
    Write-Host "      Building PacMan AMD64 Native ELF package..." -ForegroundColor Cyan
    & $pacmanBuildScript @pacmanBuildArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Host "      ERROR: PacMan Native ELF package build failed" -ForegroundColor Red
        exit 1
    }
}

# Step 1: Clean if requested
if ($Clean) {
    Write-Host "[1/6] Cleaning build artifacts..." -ForegroundColor Yellow
    
    if (Test-Path $ESPDir) {
        Remove-Item -Recurse -Force $ESPDir
        Write-Host "      Removed ESP/" -ForegroundColor Gray
    }
    
    if (Test-Path (Join-Path $BootloaderDir "guideXOS.1fedf2ad")) {
        Remove-Item -Recurse -Force (Join-Path $BootloaderDir "guideXOS.1fedf2ad")
        Write-Host "      Removed bootloader build/" -ForegroundColor Gray
    }
    
    if (Test-Path (Join-Path $BootloaderDir "x64")) {
        Remove-Item -Recurse -Force (Join-Path $BootloaderDir "x64")
        Write-Host "      Removed bootloader output/" -ForegroundColor Gray
    }
    
    if (Test-Path (Join-Path $KernelDir "build")) {
        Remove-Item -Recurse -Force (Join-Path $KernelDir "build")
        Write-Host "      Removed kernel build/" -ForegroundColor Gray
    }
    
    $RootKernelBuildDir = Join-Path $KernelDir "build\$Arch"
    if (Test-Path $RootKernelBuildDir) {
        Remove-Item -Recurse -Force $RootKernelBuildDir
        Write-Host "      Removed kernel/build/$Arch/" -ForegroundColor Gray
    }

    $LegacyRootBuildDir = Join-Path $RootDir "build\$Arch"
    if (Test-Path $LegacyRootBuildDir) {
        Remove-Item -Recurse -Force $LegacyRootBuildDir
        Write-Host "      Removed legacy build/$Arch/" -ForegroundColor Gray
    }
    
    Write-Host "      Clean complete" -ForegroundColor Green
    Write-Host ""
}

Write-Host "[1c/6] Building PacMan Native ELF package..." -ForegroundColor Yellow
Build-PacmanPackage
Write-Host ""

# Build the boot-time runtime filesystem after the package build so a clean
# build cannot embed stale PacMan metadata or executable bytes in ramdisk.img.
Write-Host "[1b/6] Building wallpaper and native-app runtime image..." -ForegroundColor Yellow
Build-WallpaperRuntimeImage
Write-Host ""

# Step 2: Build UEFI Bootloader
Write-Host "[2/6] Building UEFI Bootloader..." -ForegroundColor Yellow

# Check if bootloader exists
if (!(Test-Path $BootloaderDir)) {
    Write-Host "      ERROR: Bootloader directory not found at: $BootloaderDir" -ForegroundColor Red
    exit 1
}

# Try to build bootloader with MSBuild
$BootloaderProject = Join-Path $BootloaderDir "guideXOSBootLoader.vcxproj"
if (Test-Path $BootloaderProject) {
    Write-Host "      Building with Visual Studio..." -ForegroundColor Cyan
    
    # Find MSBuild
    $MSBuild = $null
    $VSWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    
    if (Test-Path $VSWhere) {
        $VSPath = & $VSWhere -latest -property installationPath
        if ($VSPath) {
            $MSBuild = Join-Path $VSPath "MSBuild\Current\Bin\MSBuild.exe"
        }
    }
    
    if (!$MSBuild -or !(Test-Path $MSBuild)) {
        Write-Host "      ERROR: MSBuild not found. Please install Visual Studio 2019 or later." -ForegroundColor Red
        exit 1
    }
    
    # Build bootloader
    & $MSBuild $BootloaderProject /p:Configuration=Release /p:Platform=x64 /p:GXOS_AIDA_I219_PHASE5_STAGE=$I219Phase5Stage /p:GXOS_AIDA_I219_PHASE6_STAGE=$I219Phase6Stage /p:TrackFileAccess=false /t:Rebuild /m /nologo /verbosity:minimal
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "      ERROR: Bootloader build failed" -ForegroundColor Red
        exit 1
    }
    
    Write-Host "      Bootloader built successfully" -ForegroundColor Green
} else {
    Write-Host "      ERROR: Bootloader project not found at: $BootloaderProject" -ForegroundColor Red
    exit 1
}

Write-Host ""

# Step 3: Build Kernel
if (!$SkipKernel) {
    Write-Host "[3/6] Building Kernel ($Arch)..." -ForegroundColor Yellow

    # Kernel Makefile expects to be run from the project root directory
    Push-Location $RootDir

    # Check if make is available
    # Try to find GNU make (avoid Embarcadero make or other incompatible makes)
    $Make = $null
    $MinGWBin = $null

    # Common MinGW installation paths to check
    $MinGWPaths = @(
        "C:\mingw64\bin",
        "C:\msys64\mingw64\bin",
        "C:\msys64\ucrt64\bin",
        "C:\msys2\mingw64\bin",
        "C:\MinGW\bin",
        "$env:USERPROFILE\mingw64\bin"
    )

    # Try mingw32-make first (MinGW default) from PATH
    if (Get-Command mingw32-make -ErrorAction SilentlyContinue) {
        $Make = "mingw32-make"
    }
    # Try gmake (GNU make) from PATH
    elseif (Get-Command gmake -ErrorAction SilentlyContinue) {
        $Make = "gmake"
    }
    # Try make (but verify it's GNU make) from PATH
    elseif (Get-Command make -ErrorAction SilentlyContinue) {
        try {
            $makeVersion = & make --version 2>&1 | Select-Object -First 1
            if ($makeVersion -match "GNU Make") {
                $Make = "make"
            }
        } catch {
            # Not GNU make
        }
    }

    # If not found in PATH, check common MinGW installation directories
    if (!$Make) {
        foreach ($path in $MinGWPaths) {
            $mingwMake = Join-Path $path "mingw32-make.exe"
            if (Test-Path $mingwMake) {
                $MinGWBin = $path
                $Make = $mingwMake
                Write-Host "      Found MinGW at: $path" -ForegroundColor Cyan
                # Temporarily add to PATH for this session so GCC is also available
                $env:PATH = "$path;$env:PATH"
                break
            }
        }
    }

    if (!$Make) {
        $KernelBuildSkipped = $true
        Write-Host "      WARNING: GNU make not found. Kernel build skipped." -ForegroundColor Yellow
        Write-Host ""
        Write-Host "      To build the kernel, install MinGW-w64:" -ForegroundColor Cyan
        Write-Host "        1. Download: https://github.com/niXman/mingw-builds-binaries/releases" -ForegroundColor White
        Write-Host "        2. Get: x86_64-*-release-posix-seh-ucrt-*.7z" -ForegroundColor White
        Write-Host "        3. Extract to C:\mingw64" -ForegroundColor White
        Write-Host "        4. Add C:\mingw64\bin to PATH" -ForegroundColor White
        Write-Host ""
        Write-Host "      Or use WSL:" -ForegroundColor Cyan
        Write-Host "        wsl --install" -ForegroundColor White
        Write-Host "        wsl -e bash -c 'cd kernel && make ARCH=$Arch'" -ForegroundColor White
        Write-Host ""
        Write-Host "      Continuing without kernel (bootloader is ready)..." -ForegroundColor Gray
        Write-Host ""
        Pop-Location
        
        # Don't fail - bootloader is working
    } else {
        Write-Host "      Using: $Make" -ForegroundColor Cyan

        # For x86 (i686) bare-metal builds, ensure the cross-compiler is in PATH.
        # i686-elf-tools ships separately from MinGW and lives in C:\i686-elf-tools\bin.
        $CrossCompilerPaths = @(
            "C:\i686-elf-tools\bin"
        )
        if ($Arch -eq "x86") {
            foreach ($ccPath in $CrossCompilerPaths) {
                if ((Test-Path $ccPath) -and ($env:PATH -notlike "*$ccPath*")) {
                    $env:PATH = "$ccPath;$env:PATH"
                    Write-Host "      Added cross-compiler to PATH: $ccPath" -ForegroundColor Cyan
                }
            }
            if (!(Get-Command i686-elf-g++ -ErrorAction SilentlyContinue)) {
                Write-Host "      WARNING: i686-elf-g++ not found. Install i686-elf-tools to C:\i686-elf-tools" -ForegroundColor Yellow
            }
        }

        # Remove stale final kernel outputs before invoking make so a skipped
        # or failed build cannot be mistaken for a fresh kernel.
        $KernelBinDir = Join-Path $KernelDir "build\$Arch\bin"
        Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $KernelBinDir "kernel.elf")
        Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $KernelBinDir "kernel.pe")

        # For amd64 builds, verify we have a 64-bit capable compiler
        if ($Arch -eq "amd64") {
            $gccPath = Get-Command g++ -ErrorAction SilentlyContinue
            if ($gccPath) {
                $target = & g++ -dumpmachine 2>&1
                if ($target -match "i686|i386") {
                    Write-Host "      ERROR: 32-bit compiler detected ($target)" -ForegroundColor Red
                    Write-Host "             AMD64 kernel requires a 64-bit (x86_64) toolchain" -ForegroundColor Red
                    Write-Host ""
                    Write-Host "      Your current MinGW is 32-bit only. To fix:" -ForegroundColor Yellow
                    Write-Host "        1. Download 64-bit MinGW-w64:" -ForegroundColor Cyan
                    Write-Host "           https://github.com/niXman/mingw-builds-binaries/releases" -ForegroundColor White
                    Write-Host "        2. Get: x86_64-*-release-posix-seh-ucrt-*.7z (NOT i686)" -ForegroundColor White
                    Write-Host "        3. Replace contents of C:\mingw64 with the x86_64 version" -ForegroundColor White
                    Write-Host ""
                    Pop-Location
                    exit 1
                }
            }
        }

        # Build kernel.  The kernel Makefile uses paths relative to its own
        # directory, so push into kernel/ first (mirrors what the root Makefile
        # does: cd kernel && $(MAKE) ARCH=...).
        Push-Location $KernelDir
        $KernelExtraCFlags = @()
        $KernelExtraCFlags += "-DGXOS_AIDA_I219_PHASE5_STAGE=$I219Phase5Stage"
        $KernelExtraCFlags += "-DGXOS_AIDA_I219_PHASE6_STAGE=$I219Phase6Stage"
        if (-not [string]::IsNullOrWhiteSpace($env:EXTRA_CFLAGS)) {
            $KernelExtraCFlags += $env:EXTRA_CFLAGS.Trim()
        }
        # Keep the default bare-metal build free of boot-time smoke launches.
        # The kernel makefile does not include CFLAGS in object dependencies. If
        # an opt-in launch-smoke build ran immediately before a normal build,
        # invalidate main.o so the old flagged startup path cannot be reused.
        $cleanupSmokeEnabled = ($KernelExtraCFlags -join " ") -match "GXOS_DESKTOP_CLEANUP_RUNTIME_PASS"
        if (-not $cleanupSmokeEnabled) {
            Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $KernelDir "build\$Arch\obj\core\main.o")
        }
        # The file-operation runtime smoke adds run_runtime_smoke() to the
        # clipboard translation unit. CFLAGS are not part of the Makefile's
        # object dependency key, so invalidate that object when switching
        # between normal and smoke kernels instead of allowing a stale object
        # to produce an undefined symbol at link time.
        # Always rebuild this translation unit on scripted builds so the
        # normal build after a smoke run cannot retain the smoke-only symbol.
        Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $KernelDir "build\$Arch\obj\core\file_clipboard.o")
        # The stage selector is a compile-time constant and is not part of the
        # Makefile dependency key.  Always invalidate the NIC translation unit
        # before producing a physical-test variant.
        Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $KernelDir "build\$Arch\obj\core\nic.o")
        # The Navigator kernel smoke adds the TLS capability-contract negative
        # test through EXTRA_CFLAGS.  CFLAGS are not part of the Makefile's
        # object dependency key, so invalidate both translation units that
        # consume that flag before every scripted build.  This keeps a failed
        # smoke rebuild from leaving a macro-bearing kernel_apps.o paired with
        # a normal gxos_tls_foundation.o in the next link.
        Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $KernelDir "build\$Arch\obj\core\kernel_apps.o")
        Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $KernelDir "build\$Arch\obj\core\gxos_tls_foundation.o")
        # Compiler diagnostics are written to stderr even for successful builds.
        # Keep them visible, but let the native make exit code remain the build
        # result instead of treating a warning as a terminating PowerShell error.
        $kernelOutputs = @(
            (Join-Path $KernelBinDir "kernel.elf")
        )
        if ($Arch -eq "amd64") {
            $kernelOutputs += (Join-Path $KernelBinDir "kernel.pe")
        }
        $makeResult = Invoke-GxosNativeBuildCommand `
            -FilePath $Make `
            -ArgumentList @("ARCH=$Arch", "EXTRA_CFLAGS=$($KernelExtraCFlags -join ' ')") `
            -ExpectedOutputPaths $kernelOutputs

        if (-not $makeResult.Succeeded) {
            if ($makeResult.ExitCode -ne 0) {
                Write-Host "      ERROR: Kernel build failed with process exit code $($makeResult.ExitCode)" -ForegroundColor Red
            } else {
                Write-Host "      ERROR: Kernel build reported success but expected output is missing or empty" -ForegroundColor Red
                foreach ($missingOutput in $makeResult.MissingOutputs) {
                    Write-Host "             Missing output: $missingOutput" -ForegroundColor Red
                }
            }
            Write-Host "      ERROR: Kernel build failed" -ForegroundColor Red
            Pop-Location   # kernel dir
            Pop-Location   # root dir
            exit 1
        }

        Write-Host "      Kernel built successfully" -ForegroundColor Green
        Pop-Location   # back to root
        Pop-Location   # back to original
        Write-Host ""
    }
} else {
    $KernelBuildSkipped = $true
    Write-Host "[3/6] Kernel build skipped (-SkipKernel)" -ForegroundColor Gray
    Write-Host ""
}

# Step 4: Set up ESP directory
Write-Host "[4/6] Setting up ESP directory..." -ForegroundColor Yellow

# Create ESP structure
$ESPEfiBootDir = Join-Path $ESPDir "EFI\BOOT"
if (!(Test-Path $ESPEfiBootDir)) {
    New-Item -ItemType Directory -Path $ESPEfiBootDir -Force | Out-Null
}

# Copy bootloader
$BootloaderBin = Join-Path $BootloaderDir "x64\Release\guideXOSBootLoader.exe"
if (Test-Path $BootloaderBin) {
    $TargetBootloader = Join-Path $ESPEfiBootDir "BOOTX64.EFI"
    Copy-Item $BootloaderBin $TargetBootloader -Force
    Write-Host "      Copied: BOOTX64.EFI ($(((Get-Item $TargetBootloader).Length / 1KB).ToString('0.0')) KB)" -ForegroundColor Cyan
} else {
    Write-Host "      ERROR: Bootloader binary not found at: $BootloaderBin" -ForegroundColor Red
    exit 1
}

# Copy kernel if it exists
$KernelBin = Join-Path $KernelDir "build\$Arch\bin\kernel.elf"
if ($KernelBuildSkipped) {
    Write-Host "      WARNING: Kernel build was skipped; not copying existing kernel.elf." -ForegroundColor Yellow
    Write-Host "               Run with a working AMD64 MinGW/GNU make toolchain to update ESP." -ForegroundColor Yellow
}
elseif (Test-Path $KernelBin) {
    $TargetKernel = Join-Path $ESPDir "kernel.elf"
    $NewestKernelSource = Get-ChildItem (Join-Path $RootDir "kernel") -Recurse -Include *.cpp,*.h,*.asm,*.s,Makefile,*.arch |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if ($NewestKernelSource -and (Get-Item $KernelBin).LastWriteTime -lt $NewestKernelSource.LastWriteTime) {
        Write-Host "      WARNING: Kernel binary is older than source: $($NewestKernelSource.FullName)" -ForegroundColor Yellow
        Write-Host "               ESP will receive a stale kernel unless the kernel build succeeds." -ForegroundColor Yellow
    }
    Copy-Item $KernelBin $TargetKernel -Force
    Write-Host "      Copied: kernel.elf ($(((Get-Item $TargetKernel).Length / 1KB).ToString('0.0')) KB)" -ForegroundColor Cyan
    $identityPath = Join-Path $ESPDir "build-identity.txt"
    @(
        "identity=GXOS-LARGE-FILE-PASTE-TRACE-V1"
        "probe=GXOS-LFPASTE-20260726-02"
        "imageKind=ESP-directory-used-as-QEMU-FAT-media"
        "phase5I219Stage=$I219Phase5Stage"
        "phase6I219Stage=$I219Phase6Stage"
        "imageRoot=$ESPDir"
        "bootloaderSource=$BootloaderBin"
        "bootloaderSha256=$((Get-FileHash -LiteralPath $TargetBootloader -Algorithm SHA256).Hash)"
        "kernelSource=$KernelBin"
        "kernelSha256=$((Get-FileHash -LiteralPath $KernelBin -Algorithm SHA256).Hash)"
        "espKernelSha256=$((Get-FileHash -LiteralPath $TargetKernel -Algorithm SHA256).Hash)"
        "builtAtUtc=$([DateTime]::UtcNow.ToString('o'))"
    ) | Set-Content -LiteralPath $identityPath -Encoding ascii
    Write-Host "      Wrote: build-identity.txt" -ForegroundColor Cyan
} else {
    Write-Host "      WARNING: Kernel binary not found at: $KernelBin" -ForegroundColor Yellow
    Write-Host "      ESP will boot but needs a kernel to run" -ForegroundColor Gray
}

# Ensure the boot-time runtime filesystem image exists in ESP. The bootloader
# loads this as ramdisk.img; the kernel mounts it at /system when a persistent
# root exists, or at / when booting from a release ISO without one.
Build-WallpaperRuntimeImage

# Stage the validated App Model package in the ESP root as well. The normal
# QEMU path exposes the ESP FAT volume at /, while the release ISO uses the
# same package from ramdisk.img when no persistent root is available.
function Stage-AppModelPackages {
    $packageSpecs = @(
        @{ Name = "PacMan"; Id = "com.guidexos.pacman"; Elf = "bin\amd64\pacman.elf" },
        @{ Name = "DeveloperStudio"; Id = "com.guidexos.developerstudio"; Elf = "bin\amd64\developerstudio.elf"; ExpectedElfBytes = 1015064; ExpectedElfHash = "5343F5DF0EDFE423542348C0AF8C9F8690CE339D197DEB4737DD05A27348CAC7" }
    )
    $appsRoot = Join-Path $ESPDir "Apps"
    New-Item -ItemType Directory -Path $appsRoot -Force | Out-Null
    $identityPath = Join-Path $ESPDir "build-identity.txt"

    foreach ($spec in $packageSpecs) {
        $packageRoot = Join-Path $RootDir ("Apps\" + $spec.Name)
        $manifestPath = Join-Path $packageRoot "app.json"
        $elfPath = Join-Path $packageRoot $spec.Elf
        foreach ($required in @(
            @{ Path = $manifestPath; Name = "$($spec.Name) manifest" },
            @{ Path = $elfPath; Name = "$($spec.Name) AMD64 Native ELF" }
        )) {
            if (!(Test-Path -LiteralPath $required.Path -PathType Leaf)) {
                Write-Host "      ERROR: Missing $($required.Name): $($required.Path)" -ForegroundColor Red
                exit 1
            }
        }

        try {
            $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
            $entry = @($manifest.entries | Where-Object { $_.architecture -eq $Arch })
            if ($manifest.id -ne $spec.Id -or $manifest.kind -ne "NativeElf" -or
                $entry.Count -ne 1 -or $entry[0].path -ne ($spec.Elf.Replace('\', '/')) -or
                $entry[0].entryPoint -ne "gx_main" -or $entry[0].abi -ne "guidexos-c-abi-v1" -or
                $entry[0].runtime -ne "native-elf") {
                throw "manifest identity, kind, architecture, path, entry point, ABI, or runtime is invalid"
            }
        } catch {
            Write-Host "      ERROR: $($spec.Name) App Model manifest validation failed: $($_.Exception.Message)" -ForegroundColor Red
            exit 1
        }

        $elfBytes = [IO.File]::ReadAllBytes($elfPath)
        if ($elfBytes.Length -lt 20 -or $elfBytes[0] -ne 0x7f -or $elfBytes[1] -ne 0x45 -or
            $elfBytes[2] -ne 0x4c -or $elfBytes[3] -ne 0x46 -or $elfBytes[4] -ne 2 -or
            $elfBytes[5] -ne 1 -or $elfBytes[18] -ne 0x3e -or $elfBytes[19] -ne 0) {
            Write-Host "      ERROR: $($spec.Name) executable is not a little-endian AMD64 ELF64" -ForegroundColor Red
            exit 1
        }
        $sourceElfHash = (Get-FileHash -LiteralPath $elfPath -Algorithm SHA256).Hash.ToUpperInvariant()
        if ($spec.ExpectedElfBytes -and ($elfBytes.Length -ne $spec.ExpectedElfBytes -or $sourceElfHash -ne $spec.ExpectedElfHash)) {
            Write-Host "      ERROR: $($spec.Name) signed-off ELF size/hash changed; refusing to stage it" -ForegroundColor Red
            exit 1
        }

        $stageRoot = Join-Path $appsRoot $spec.Name
        if (Test-Path -LiteralPath $stageRoot) {
            $resolvedAppsRoot = (Resolve-Path -LiteralPath $appsRoot).Path.TrimEnd('\')
            $resolvedStageRoot = (Resolve-Path -LiteralPath $stageRoot).Path
            if (!$resolvedStageRoot.StartsWith($resolvedAppsRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
                Write-Host "      ERROR: refusing to remove package stage outside ESP\Apps: $resolvedStageRoot" -ForegroundColor Red
                exit 1
            }
            Remove-Item -LiteralPath $resolvedStageRoot -Recurse -Force
        }
        New-Item -ItemType Directory -Path $stageRoot -Force | Out-Null
        $sourceFiles = @(Get-ChildItem -LiteralPath $packageRoot -Recurse -File)
        foreach ($sourceFile in $sourceFiles) {
            $relative = $sourceFile.FullName.Substring($packageRoot.Length + 1)
            $target = Join-Path $stageRoot $relative
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
            Copy-Item -LiteralPath $sourceFile.FullName -Destination $target -Force
        }

        $actualFiles = @(Get-ChildItem -LiteralPath $stageRoot -Recurse -File | ForEach-Object {
            $_.FullName.Substring($stageRoot.Length + 1).Replace('\', '/')
        } | Sort-Object)
        $expectedFiles = @($sourceFiles | ForEach-Object {
            $_.FullName.Substring($packageRoot.Length + 1).Replace('\', '/')
        } | Sort-Object)
        if ((Compare-Object $expectedFiles $actualFiles).Count -ne 0) {
            Write-Host "      ERROR: staged $($spec.Name) package tree is not exact: $($actualFiles -join ', ')" -ForegroundColor Red
            exit 1
        }

        $stagedElf = Join-Path $stageRoot $spec.Elf
        $stagedManifest = Join-Path $stageRoot "app.json"
        $elfHash = (Get-FileHash -LiteralPath $stagedElf -Algorithm SHA256).Hash
        $manifestHash = (Get-FileHash -LiteralPath $stagedManifest -Algorithm SHA256).Hash
        Write-Host "      Staged /Apps/$($spec.Name) App Model package ($($actualFiles -join ', '))" -ForegroundColor Green
        Write-Host "      $($spec.Name) ELF bytes=$((Get-Item -LiteralPath $stagedElf).Length) sha256=$elfHash" -ForegroundColor Cyan
        Write-Host "      $($spec.Name) manifest bytes=$((Get-Item -LiteralPath $stagedManifest).Length) sha256=$manifestHash" -ForegroundColor Cyan
        if (Test-Path -LiteralPath $identityPath) {
            $identityPrefix = $spec.Name.ToLowerInvariant()
            Add-Content -LiteralPath $identityPath -Value @(
                "${identityPrefix}PackageRoot=/Apps/$($spec.Name)"
                "${identityPrefix}ElfSha256=$elfHash"
                "${identityPrefix}ManifestSha256=$manifestHash"
                "${identityPrefix}ElfBytes=$((Get-Item -LiteralPath $stagedElf).Length)"
                "${identityPrefix}ManifestBytes=$((Get-Item -LiteralPath $stagedManifest).Length)"
            )
        }
    }
}
Stage-AppModelPackages

Write-Host "      ESP directory ready" -ForegroundColor Green
Write-Host ""

# Step 5: Display ESP structure
Write-Host "[5/6] ESP Directory Structure:" -ForegroundColor Yellow
Write-Host ""

function Show-Tree {
    param($Path, $Indent = "")
    
    $items = Get-ChildItem $Path | Sort-Object { $_.PSIsContainer }, Name
    
    foreach ($item in $items) {
        if ($item.PSIsContainer) {
            Write-Host "$Indent??? $($item.Name)/" -ForegroundColor Blue
            Show-Tree $item.FullName "$Indent?   "
        } else {
            $size = if ($item.Length -lt 1KB) {
                "$($item.Length) bytes"
            } elseif ($item.Length -lt 1MB) {
                "$([math]::Round($item.Length / 1KB, 1)) KB"
            } else {
                "$([math]::Round($item.Length / 1MB, 1)) MB"
            }
            Write-Host "$Indent??? $($item.Name) ($size)" -ForegroundColor Gray
        }
    }
}

Write-Host "ESP/" -ForegroundColor Blue
Show-Tree $ESPDir ""
Write-Host ""

# Step 6: Check prerequisites for running
Write-Host "[6/6] Checking QEMU prerequisites..." -ForegroundColor Yellow

$AllReady = $true

# Check OVMF.fd (case-insensitive)
$OVMF = Join-Path $RootDir "OVMF.fd"
$OVMFLower = Join-Path $RootDir "ovmf.fd"
$QemuOVMF = "C:\Program Files\qemu\share\edk2-x86_64-code.fd"
if (Test-Path $OVMF) {
    Write-Host "      ? OVMF.fd found" -ForegroundColor Green
} elseif (Test-Path $OVMFLower) {
    $OVMF = $OVMFLower
    Write-Host "      ? ovmf.fd found" -ForegroundColor Green
} elseif (Test-Path $QemuOVMF) {
    Write-Host "      ? Using QEMU's built-in UEFI firmware" -ForegroundColor Green
    $OVMF = $QemuOVMF
} else {
    Write-Host "      ? UEFI firmware not found" -ForegroundColor Yellow
    Write-Host "        QEMU's built-in EDK2 firmware will be used if available" -ForegroundColor Gray
    $AllReady = $false
}

# Check QEMU
$Qemu = Get-Command qemu-system-x86_64 -ErrorAction SilentlyContinue
if (!$Qemu) {
    # Check common QEMU installation paths
    $QemuPaths = @(
        "C:\Program Files\qemu\qemu-system-x86_64.exe",
        "C:\qemu\qemu-system-x86_64.exe",
        "$env:USERPROFILE\qemu\qemu-system-x86_64.exe"
    )
    foreach ($qpath in $QemuPaths) {
        if (Test-Path $qpath) {
            $Qemu = [PSCustomObject]@{ Source = $qpath }
            break
        }
    }
}
if (!$Qemu) {
    Write-Host "      ? QEMU not found in PATH" -ForegroundColor Yellow
    Write-Host "        Download from: https://www.qemu.org/download/#windows" -ForegroundColor Gray
    Write-Host "        Add to PATH: C:\Program Files\qemu" -ForegroundColor Gray
    $AllReady = $false
} else {
    Write-Host "      ? QEMU found: $($Qemu.Source)" -ForegroundColor Green
}

# Check kernel
if (!(Test-Path (Join-Path $ESPDir "kernel.elf"))) {
    Write-Host "      ? kernel.elf not in ESP" -ForegroundColor Yellow
    Write-Host "        Install MinGW and rebuild to create kernel" -ForegroundColor Gray
    $AllReady = $false
} else {
    $EspKernel = Get-Item (Join-Path $ESPDir "kernel.elf")
    $BuiltKernel = Join-Path $KernelDir "build\$Arch\bin\kernel.elf"
    $NewestKernelSource = Get-ChildItem (Join-Path $RootDir "kernel") -Recurse -Include *.cpp,*.h,*.asm,*.s,Makefile,*.arch |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if ($KernelBuildSkipped) {
        Write-Host "      ? kernel.elf in ESP was not updated this run" -ForegroundColor Yellow
        Write-Host "        Kernel build was skipped, so this may still be an old boot image." -ForegroundColor Gray
        $AllReady = $false
    } elseif (!(Test-Path $BuiltKernel)) {
        Write-Host "      ? built kernel missing at kernel/build/$Arch/bin/kernel.elf" -ForegroundColor Yellow
        $AllReady = $false
    } elseif ($NewestKernelSource -and (Get-Item $BuiltKernel).LastWriteTime -lt $NewestKernelSource.LastWriteTime) {
        Write-Host "      ? built kernel is older than source" -ForegroundColor Yellow
        Write-Host "        Newer source: $($NewestKernelSource.FullName)" -ForegroundColor Gray
        $AllReady = $false
    } elseif ($NewestKernelSource -and $EspKernel.LastWriteTime -lt $NewestKernelSource.LastWriteTime) {
        Write-Host "      ? kernel.elf in ESP is stale" -ForegroundColor Yellow
        Write-Host "        Newer source: $($NewestKernelSource.FullName)" -ForegroundColor Gray
        $AllReady = $false
    } else {
        Write-Host "      ? kernel.elf in ESP" -ForegroundColor Green
    }
}

Write-Host ""

# Summary
Write-Host "====================================" -ForegroundColor Green
Write-Host "  Build Complete!" -ForegroundColor Green
Write-Host "====================================" -ForegroundColor Green
Write-Host ""

if ($AllReady) {
    Write-Host "✓ All prerequisites met!" -ForegroundColor Green
    Write-Host ""
    if ($RunQemu) {
        if ($FsTest) {
            # Delegate entirely to the canonical filesystem test script so QEMU
            # arguments, disk image paths, IDE drive ordering, machine type,
            # display and network settings all stay in one place.
            Write-Host "Delegating to scripts\run-qemu-fs-test.ps1 (canonical FS test path)..." -ForegroundColor Cyan
            Write-Host ""
            $FsTestScript = Join-Path $RootDir "scripts\run-qemu-fs-test.ps1"
            if (!(Test-Path $FsTestScript)) {
                Write-Host "ERROR: scripts\run-qemu-fs-test.ps1 not found at: $FsTestScript" -ForegroundColor Red
                Write-Host "Cannot run filesystem test." -ForegroundColor Red
                exit 1
            }
            # Forward compatible switches
            $fsArgs = @{}
            if ($Fat32Only) { $fsArgs["Fat32Only"] = $true }
            if ($Ext4Only)  { $fsArgs["Ext4Only"]  = $true }
            if ($Debug)     { $fsArgs["Debug"]      = $true }
            if ($WaitGdb)   { $fsArgs["WaitGdb"]    = $true }
            $fsArgs["Memory"] = $Memory
            & $FsTestScript @fsArgs
        } else {
            Write-Host "Launching QEMU..." -ForegroundColor Cyan
            Write-Host ""

            $QemuArgs = @(
                "-machine", "pc",
                "-drive", "if=pflash,format=raw,readonly=on,file=$OVMF",
                "-drive", "file=fat:rw:$ESPDir,format=raw,if=ide,index=0",
                "-m", $Memory,
                "-vga", "std",
                "-serial", "stdio",
                "-no-reboot",
                "-rtc", "base=utc,clock=host",
                "-netdev", "user,id=net0",
                "-device", "e1000,netdev=net0"
            )
            $QemuArgs += Get-GxosQemuSecureRngArguments
            if ($Debug) {
                $QemuArgs += "-d", "int,cpu_reset"
                $QemuArgs += "-D", "qemu-debug.log"
            }
            if ($WaitGdb) {
                $QemuArgs += "-s", "-S"
                Write-Host "Waiting for GDB connection on localhost:1234..." -ForegroundColor Yellow
            }

            Push-Location $RootDir
            try { & $Qemu.Source $QemuArgs } finally { Pop-Location }
        }
    } else {
        Write-Host "To run in QEMU:" -ForegroundColor Cyan
        Write-Host "  .\build.ps1 -RunQemu" -ForegroundColor White
        Write-Host ""
        Write-Host "To run filesystem tests (canonical path):" -ForegroundColor Cyan
        Write-Host "  .\scripts\run-qemu-fs-test.ps1" -ForegroundColor White
        Write-Host ""
        Write-Host "  NOTE: '.\build.ps1 -RunQemu -FsTest' delegates to the above script." -ForegroundColor Gray
        Write-Host "  Use scripts\run-qemu-fs-test.ps1 directly for full control." -ForegroundColor Gray
    }
} else {
    Write-Host "? Some prerequisites missing" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Current status:" -ForegroundColor Cyan
    Write-Host "  Bootloader: ? Built successfully" -ForegroundColor Green
    Write-Host "  Kernel: " -NoNewline
    if ($KernelBuildSkipped) {
        Write-Host "? Not updated (kernel build skipped)" -ForegroundColor Yellow
    } elseif (Test-Path (Join-Path $ESPDir "kernel.elf")) {
        Write-Host "? Available" -ForegroundColor Green
    } else {
        Write-Host "? Not built (install MinGW)" -ForegroundColor Yellow
    }
    Write-Host "  OVMF: " -NoNewline
    if (Test-Path $OVMF) {
        Write-Host "? Available" -ForegroundColor Green
    } else {
        Write-Host "? Download needed" -ForegroundColor Yellow
    }
    Write-Host "  QEMU: " -NoNewline
    if ($Qemu) {
        Write-Host "? Installed" -ForegroundColor Green
    } else {
        Write-Host "? Install needed" -ForegroundColor Yellow
    }
    Write-Host ""
    Write-Host "See instructions above to complete setup" -ForegroundColor Gray
}

Write-Host ""
