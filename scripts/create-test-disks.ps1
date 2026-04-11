#
# create-test-disks.ps1
# Creates FAT32 and ext4 test disk images for guideXOS filesystem testing
#
# Usage: 
#   PowerShell -ExecutionPolicy Bypass -File create-test-disks.ps1
#   Or run as Administrator for full functionality
#
# Requirements:
#   - Windows 10/11
#   - For ext4: WSL2 installed, OR just create FAT32 only
#
# Copyright (c) 2025 guideXOS Server
#

param(
    [int]$DiskSizeMB = 64,
    [switch]$Fat32Only,
    [switch]$Force
)

$ErrorActionPreference = "Stop"

# Configuration
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$DiskDir = Join-Path (Split-Path -Parent $ScriptDir) "disks"

# Colors for output
function Write-Info { Write-Host "[INFO] $args" -ForegroundColor Green }
function Write-Warn { Write-Host "[WARN] $args" -ForegroundColor Yellow }
function Write-Err { Write-Host "[ERROR] $args" -ForegroundColor Red }

function Test-Administrator {
    $currentUser = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($currentUser)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Test-WSL {
    try {
        $result = wsl --status 2>&1
        return $LASTEXITCODE -eq 0
    } catch {
        return $false
    }
}

function Initialize-Directories {
    Write-Info "Setting up directories..."
    
    if (-not (Test-Path $DiskDir)) {
        New-Item -ItemType Directory -Path $DiskDir -Force | Out-Null
    }
}

function New-TestContent {
    param([string]$MountPath)
    
    Write-Info "Creating test files..."
    
    # Root level test file
    @"
Hello from guideXOS filesystem test!
This file tests basic root directory reading.
"@ | Out-File -FilePath (Join-Path $MountPath "test.txt") -Encoding ASCII
    
    # Create apps directory
    $appsDir = Join-Path $MountPath "apps"
    New-Item -ItemType Directory -Path $appsDir -Force | Out-Null
    
    # Text file in subdirectory
    @"
This file is in the /apps subdirectory.
Testing subdirectory traversal.
"@ | Out-File -FilePath (Join-Path $appsDir "hello.txt") -Encoding ASCII
    
    # Small binary file (1KB) with random data
    $randomBytes = New-Object byte[] 1024
    $rng = [System.Security.Cryptography.RandomNumberGenerator]::Create()
    $rng.GetBytes($randomBytes)
    [System.IO.File]::WriteAllBytes((Join-Path $appsDir "test.bin"), $randomBytes)
    
    # Large binary file (1MB) 
    $largeBytes = New-Object byte[] (1024 * 1024)
    $rng.GetBytes($largeBytes)
    [System.IO.File]::WriteAllBytes((Join-Path $appsDir "large.bin"), $largeBytes)
    $rng.Dispose()
    
    # Mock .gxapp file
    @"
GXAPP
VERSION: 1
CREATED: 2025-01-01T00:00:00Z
GENERATOR: create-test-disks.ps1

This is a mock gxapp file for testing file reading.
In production, this would be a ZIP archive containing:
- metadata.json
- bin/x86/app.elf
- bin/amd64/app.elf
"@ | Out-File -FilePath (Join-Path $appsDir "sample.gxapp") -Encoding ASCII
    
    # Create nested directory structure
    $deepDir = Join-Path $MountPath "deep\nested\directory"
    New-Item -ItemType Directory -Path $deepDir -Force | Out-Null
    "Deep nested file" | Out-File -FilePath (Join-Path $deepDir "deep.txt") -Encoding ASCII
    
    # Empty file
    New-Item -ItemType File -Path (Join-Path $MountPath "empty.txt") -Force | Out-Null
    
    # README
    @"
guideXOS Filesystem Test Disk
==============================

This disk image contains test files for validating the guideXOS
Virtual Filesystem (VFS) layer.

Directory Structure:
  /test.txt           - Simple text file (root)
  /empty.txt          - Empty file
  /README.txt         - This file
  /apps/              - Application directory
  /apps/hello.txt     - Text file in subdirectory
  /apps/test.bin      - 1KB binary file
  /apps/large.bin     - 1MB binary file
  /apps/sample.gxapp  - Mock gxapp package
  /deep/nested/...    - Deeply nested directory

Test Scenarios:
  1. Mount filesystem
  2. Read /test.txt
  3. List /apps directory
  4. Read /apps/hello.txt (subdirectory)
  5. Read /apps/large.bin (multi-cluster)
  6. Traverse deep directories
"@ | Out-File -FilePath (Join-Path $MountPath "README.txt") -Encoding ASCII
    
    Write-Info "Test files created."
}

function New-Fat32Image {
    $imagePath = Join-Path $DiskDir "test-fat32.img"
    
    Write-Info "Creating FAT32 disk image ($DiskSizeMB MB)..."
    
    # Remove existing image
    if (Test-Path $imagePath) {
        if (-not $Force) {
            $response = Read-Host "Image exists. Overwrite? (y/N)"
            if ($response -ne 'y' -and $response -ne 'Y') {
                Write-Warn "Skipping FAT32 image creation."
                return
            }
        }
        Remove-Item $imagePath -Force
    }
    
    # Method 1: Using diskpart (requires admin)
    if (Test-Administrator) {
        Write-Info "Using Windows native tools (Administrator mode)..."
        
        # Create VHD
        $vhdPath = $imagePath -replace '\.img$', '.vhd'
        
        $diskpartScript = @"
create vdisk file="$vhdPath" maximum=$DiskSizeMB type=fixed
select vdisk file="$vhdPath"
attach vdisk
create partition primary
format fs=fat32 quick label="GXOSTEST"
assign letter=Z
"@
        $diskpartScript | diskpart | Out-Null
        
        # Create test content
        New-TestContent -MountPath "Z:\"
        
        # Calculate checksums
        $checksumFile = Join-Path $DiskDir "test-fat32-checksums.txt"
        $hash1 = Get-FileHash "Z:\apps\test.bin" -Algorithm SHA256
        $hash2 = Get-FileHash "Z:\apps\large.bin" -Algorithm SHA256
        @"
$($hash1.Hash)  apps/test.bin
$($hash2.Hash)  apps/large.bin
"@ | Out-File $checksumFile -Encoding ASCII
        
        # Detach VHD
        $detachScript = @"
select vdisk file="$vhdPath"
detach vdisk
"@
        $detachScript | diskpart | Out-Null
        
        # Convert VHD to raw IMG
        # Note: This requires qemu-img or similar tool
        if (Get-Command qemu-img -ErrorAction SilentlyContinue) {
            qemu-img convert -f vpc -O raw $vhdPath $imagePath
            Remove-Item $vhdPath -Force
        } else {
            Write-Warn "qemu-img not found. VHD file created instead of raw IMG."
            Write-Warn "Rename $vhdPath to use with QEMU, or install qemu-img."
            Move-Item $vhdPath $imagePath -Force
        }
    }
    # Method 2: Using WSL
    elseif (Test-WSL) {
        Write-Info "Using WSL to create FAT32 image..."
        
        $wslDiskDir = wsl wslpath -u "'$DiskDir'"
        $wslImagePath = wsl wslpath -u "'$imagePath'"
        
        wsl bash -c "dd if=/dev/zero of=$wslImagePath bs=1M count=$DiskSizeMB 2>/dev/null"
        wsl bash -c "mkfs.fat -F 32 -n GXOSTEST $wslImagePath"
        
        # Mount in WSL and create content
        $tempMount = "/tmp/guidexos-test-$$"
        wsl bash -c "mkdir -p $tempMount && sudo mount -o loop $wslImagePath $tempMount"
        
        # Create files via WSL
        wsl bash -c "echo 'Hello from guideXOS filesystem test!' > $tempMount/test.txt"
        wsl bash -c "mkdir -p $tempMount/apps"
        wsl bash -c "echo 'This file is in the /apps subdirectory.' > $tempMount/apps/hello.txt"
        wsl bash -c "dd if=/dev/urandom of=$tempMount/apps/test.bin bs=1024 count=1 2>/dev/null"
        wsl bash -c "dd if=/dev/urandom of=$tempMount/apps/large.bin bs=1024 count=1024 2>/dev/null"
        wsl bash -c "echo 'GXAPP' > $tempMount/apps/sample.gxapp"
        wsl bash -c "mkdir -p $tempMount/deep/nested/directory"
        wsl bash -c "echo 'Deep nested file' > $tempMount/deep/nested/directory/deep.txt"
        wsl bash -c "touch $tempMount/empty.txt"
        
        # Checksums
        wsl bash -c "sha256sum $tempMount/apps/test.bin $tempMount/apps/large.bin" | Out-File (Join-Path $DiskDir "test-fat32-checksums.txt") -Encoding ASCII
        
        # Unmount
        wsl bash -c "sudo umount $tempMount && rmdir $tempMount"
    }
    # Method 3: Pure PowerShell (limited - creates directory structure only)
    else {
        Write-Warn "Neither Administrator mode nor WSL available."
        Write-Warn "Creating a directory structure instead of disk image."
        Write-Warn "You'll need to use Linux/WSL to create the actual disk image."
        
        $mockDir = Join-Path $DiskDir "test-fat32-contents"
        if (Test-Path $mockDir) { Remove-Item $mockDir -Recurse -Force }
        New-Item -ItemType Directory -Path $mockDir -Force | Out-Null
        
        New-TestContent -MountPath $mockDir
        
        Write-Info "Directory structure created at: $mockDir"
        Write-Info "To create actual disk image, run in WSL:"
        Write-Info "  cd $mockDir && tar cf - . | (cd /tmp && dd if=/dev/zero of=test.img bs=1M count=64 && mkfs.fat -F32 test.img && sudo mount -o loop test.img /mnt && sudo tar xf - -C /mnt && sudo umount /mnt)"
        return
    }
    
    Write-Info "FAT32 image created: $imagePath"
    Get-Item $imagePath | Select-Object Name, Length, LastWriteTime
}

function New-Ext4Image {
    $imagePath = Join-Path $DiskDir "test-ext4.img"
    
    Write-Info "Creating ext4 disk image ($DiskSizeMB MB)..."
    
    if (-not (Test-WSL)) {
        Write-Warn "ext4 creation requires WSL. Skipping ext4 image."
        Write-Warn "Install WSL2 and run again, or create ext4 image on Linux."
        return
    }
    
    # Remove existing image
    if (Test-Path $imagePath) {
        if (-not $Force) {
            $response = Read-Host "Image exists. Overwrite? (y/N)"
            if ($response -ne 'y' -and $response -ne 'Y') {
                Write-Warn "Skipping ext4 image creation."
                return
            }
        }
        Remove-Item $imagePath -Force
    }
    
    $wslImagePath = wsl wslpath -u "'$imagePath'"
    $tempMount = "/tmp/guidexos-ext4-$$"
    
    Write-Info "Using WSL to create ext4 image..."
    
    wsl bash -c "dd if=/dev/zero of=$wslImagePath bs=1M count=$DiskSizeMB 2>/dev/null"
    wsl bash -c "mke2fs -t ext4 -L GXOSTEST -m 0 $wslImagePath 2>/dev/null"
    
    # Mount and create content
    wsl bash -c "mkdir -p $tempMount && sudo mount -o loop $wslImagePath $tempMount"
    
    wsl bash -c "echo 'Hello from guideXOS filesystem test!' > $tempMount/test.txt"
    wsl bash -c "mkdir -p $tempMount/apps"
    wsl bash -c "echo 'This file is in the /apps subdirectory.' > $tempMount/apps/hello.txt"
    wsl bash -c "dd if=/dev/urandom of=$tempMount/apps/test.bin bs=1024 count=1 2>/dev/null"
    wsl bash -c "dd if=/dev/urandom of=$tempMount/apps/large.bin bs=1024 count=1024 2>/dev/null"
    wsl bash -c "echo 'GXAPP' > $tempMount/apps/sample.gxapp"
    wsl bash -c "mkdir -p $tempMount/deep/nested/directory"
    wsl bash -c "echo 'Deep nested file' > $tempMount/deep/nested/directory/deep.txt"
    wsl bash -c "touch $tempMount/empty.txt"
    
    # Checksums
    wsl bash -c "sha256sum $tempMount/apps/test.bin $tempMount/apps/large.bin" | Out-File (Join-Path $DiskDir "test-ext4-checksums.txt") -Encoding ASCII
    
    # Unmount
    wsl bash -c "sudo umount $tempMount && rmdir $tempMount"
    
    Write-Info "ext4 image created: $imagePath"
    Get-Item $imagePath | Select-Object Name, Length, LastWriteTime
}

# Main execution
function Main {
    Write-Host "=============================================="
    Write-Host "guideXOS Test Disk Image Creator (Windows)"
    Write-Host "=============================================="
    Write-Host ""
    
    if (Test-Administrator) {
        Write-Info "Running as Administrator - full functionality available."
    } else {
        Write-Warn "Not running as Administrator."
        Write-Warn "Some features may require WSL or will be limited."
    }
    
    if (Test-WSL) {
        Write-Info "WSL detected - can create ext4 images."
    } else {
        Write-Warn "WSL not detected - ext4 creation will be skipped."
    }
    
    Write-Host ""
    
    Initialize-Directories
    
    New-Fat32Image
    Write-Host ""
    
    if (-not $Fat32Only) {
        New-Ext4Image
    }
    
    Write-Host ""
    Write-Host "=============================================="
    Write-Info "Disk image creation complete!"
    Write-Host ""
    Write-Host "Images location: $DiskDir"
    Write-Host ""
    Write-Host "Files created:"
    Get-ChildItem $DiskDir -Filter "*.img" | Format-Table Name, Length, LastWriteTime
    Write-Host ""
    Write-Host "To use with QEMU:"
    Write-Host "  qemu-system-x86_64 -drive file=$DiskDir\test-fat32.img,format=raw,if=ide"
    Write-Host ""
    Write-Host "Or run: .\scripts\run-qemu-fs-test.ps1"
    Write-Host "=============================================="
}

Main
