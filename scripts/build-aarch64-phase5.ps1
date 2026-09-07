[CmdletBinding()]
param(
    [string]$LlvmRoot = 'C:\Program Files\LLVM\bin',
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) { $OutputDirectory = Join-Path $repoRoot 'out\aarch64-phase5' }
else { $OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory) }
$rootPrefix = $repoRoot.TrimEnd('\') + '\out\'
if (!$OutputDirectory.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) { throw "Refusing output outside repository out/: $OutputDirectory" }

$clang = Join-Path $LlvmRoot 'clang++.exe'
$ld = Join-Path $LlvmRoot 'ld.lld.exe'
$lldLink = Join-Path $LlvmRoot 'lld-link.exe'
$llvmReadobj = Join-Path $LlvmRoot 'llvm-readobj.exe'
foreach ($tool in @($clang, $ld, $lldLink, $llvmReadobj)) { if (!(Test-Path -LiteralPath $tool -PathType Leaf)) { throw "Required LLVM tool not found: $tool" } }
function Invoke-Checked { param([string]$FilePath, [string[]]$Arguments); Write-Host "      $([IO.Path]::GetFileName($FilePath)) $($Arguments -join ' ')" -ForegroundColor DarkGray; & $FilePath @Arguments; if ($LASTEXITCODE -ne 0) { throw "Command failed ($LASTEXITCODE): $FilePath" } }
function Read-U16([byte[]]$Bytes, [int]$Offset) { return [int]$Bytes[$Offset] -bor ([int]$Bytes[$Offset + 1] -shl 8) }
function Read-U32([byte[]]$Bytes, [int]$Offset) { return [uint32]$Bytes[$Offset] -bor ([uint32]$Bytes[$Offset + 1] -shl 8) -bor ([uint32]$Bytes[$Offset + 2] -shl 16) -bor ([uint32]$Bytes[$Offset + 3] -shl 24) }
function Test-PeArm64([string]$Path) { $b=[IO.File]::ReadAllBytes($Path); if($b.Length -lt 64){return $false}; $p=[int](Read-U32 $b 60); return $p -ge 0 -and $p+6 -le $b.Length -and $b[$p]-eq 0x50 -and $b[$p+1]-eq 0x45 -and (Read-U16 $b ($p+4))-eq 0xaa64 }
function Test-Elf([string]$Path, [int]$Machine) { $b=[IO.File]::ReadAllBytes($Path); return $b.Length -ge 20 -and $b[0]-eq 0x7f -and $b[1]-eq 0x45 -and $b[2]-eq 0x4c -and $b[3]-eq 0x46 -and $b[4]-eq 2 -and $b[5]-eq 1 -and (Read-U16 $b 18)-eq $Machine }

if (Test-Path -LiteralPath $OutputDirectory) { Remove-Item -LiteralPath $OutputDirectory -Recurse -Force }
$null = New-Item -ItemType Directory -Path (Join-Path $OutputDirectory 'esp\EFI\BOOT') -Force

$loaderObject = Join-Path $OutputDirectory 'phase5_loader.obj'
$efiPath = Join-Path $OutputDirectory 'BOOTAA64.EFI'
$loaderSource = Join-Path $repoRoot 'guideXOSBootLoader\aarch64\phase1_loader.cpp'
$loaderFlags = @('--target=aarch64-pc-windows-msvc', '-DGXOS_AARCH64_PHASE4', '-O2', '-ffreestanding', '-fno-builtin', '-fno-stack-protector', '-fno-exceptions', '-fno-rtti', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables', '-fno-ident', '-I', (Join-Path $repoRoot 'guideXOSBootLoader'), '-I', $repoRoot, '-c', $loaderSource, '-o', $loaderObject)
Write-Host '[1/10] Compiling ARM64 UEFI loader...' -ForegroundColor Yellow
Invoke-Checked $clang $loaderFlags
Write-Host '[2/10] Linking BOOTAA64.EFI...' -ForegroundColor Yellow
Invoke-Checked $lldLink @('/subsystem:efi_application','/entry:efi_main','/nodefaultlib','/machine:arm64','/timestamp:0',"/out:$efiPath",$loaderObject)
if (!(Test-PeArm64 $efiPath)) { throw 'BOOTAA64.EFI failed ARM64 PE verification' }

$kernelSources = @(
    'kernel\arch\arm64\phase3_entry.S','kernel\arch\arm64\phase3_context.S','kernel\arch\arm64\phase3_register_test.S','kernel\arch\arm64\phase3_preemptive_worker.S','kernel\arch\arm64\native_elf_call.S',
    'kernel\arch\arm64\phase3_serial.cpp','kernel\arch\arm64\phase4_serial.cpp','kernel\arch\arm64\phase2_platform.cpp','kernel\arch\arm64\phase2_mmu.cpp','kernel\arch\arm64\phase5_mmu_permissions.cpp','kernel\arch\arm64\phase2_gic.cpp','kernel\arch\arm64\phase3_timer.cpp','kernel\arch\arm64\phase3_arch.cpp','kernel\arch\arm64\phase3_exceptions.cpp','kernel\arch\arm64\phase4_main.cpp',
    'kernel\core\common_scheduler.cpp','kernel\core\common_kernel_entry.cpp','kernel\core\common_boot_info.cpp','kernel\core\common_physical_allocator.cpp','kernel\core\irq_registry.cpp','kernel\core\cxx_runtime.cpp','kernel\core\block_device.cpp','kernel\core\ramdisk.cpp','kernel\core\vfs.cpp','kernel\core\fs_fat.cpp','kernel\core\fs_ext4.cpp','kernel\core\native_elf_baremetal.cpp')
$kernelObjects = @()
$kernelFlags = @('--target=aarch64-none-elf','-std=c++14','-march=armv8-a','-O2','-ffreestanding','-nostdlib','-nostdinc++','-fno-builtin','-fno-stack-protector','-fno-exceptions','-fno-rtti','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-fno-pic','-fno-pie','-mcmodel=small','-mgeneral-regs-only','-mstrict-align','-mno-outline-atomics','-DGXOS_AARCH64_PHASE4','-DGXOS_AARCH64_PHASE5','-I',$repoRoot,'-I',(Join-Path $repoRoot 'kernel\core\include'),'-I',(Join-Path $repoRoot 'kernel\arch\arm64\include'))
Write-Host '[3/10] Compiling common kernel, VFS, App Model, and ARM64 boundary...' -ForegroundColor Yellow
foreach ($relative in $kernelSources) { $source=Join-Path $repoRoot $relative; $leaf=[IO.Path]::GetFileName($relative); $object=Join-Path $OutputDirectory (($leaf -replace '\.S$','.o' -replace '\.cpp$','.o')); Invoke-Checked $clang ($kernelFlags + @('-c',$source,'-o',$object)); $kernelObjects += $object }
Write-Host '[4/10] Linking Phase 5 kernel.elf...' -ForegroundColor Yellow
$kernelPath = Join-Path $OutputDirectory 'kernel.elf'
Invoke-Checked $ld (@('-m','aarch64elf','-T',(Join-Path $repoRoot 'kernel\arch\arm64\phase3_linker.ld'),'-o',$kernelPath) + $kernelObjects)
if (!(Test-Elf $kernelPath 183)) { throw 'kernel.elf failed EM_AARCH64 verification' }

$appObject = Join-Path $OutputDirectory 'phase5-arm64-proof.o'
$appElf = Join-Path $OutputDirectory 'phase5-arm64-proof.elf'
$appSource = Join-Path $repoRoot 'sdk\samples\phase5_arm64\main.cpp'
$appFlags = @('--target=aarch64-none-elf','-std=c++14','-march=armv8-a','-O2','-ffreestanding','-nostdlib','-nostdinc++','-fno-builtin','-fno-exceptions','-fno-rtti','-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-fno-pic','-fno-pie','-mcmodel=small','-mgeneral-regs-only','-mstrict-align','-mno-outline-atomics','-I',(Join-Path $repoRoot 'sdk\include'),'-c',$appSource,'-o',$appObject)
Write-Host '[5/10] Compiling independent AArch64 proof application...' -ForegroundColor Yellow
Invoke-Checked $clang $appFlags
Write-Host '[6/10] Linking ET_EXEC application at 0x50000000...' -ForegroundColor Yellow
Invoke-Checked $ld @('-m','aarch64elf','--image-base=0x50000000','-z','max-page-size=0x1000','-e','gx_main','-o',$appElf,$appObject)
if (!(Test-Elf $appElf 183)) { throw 'proof application failed ELF64 EM_AARCH64 verification' }

Write-Host '[7/10] Running Phase 5 host negative controls...' -ForegroundColor Yellow
$hostCxx = 'C:\mingw64\bin\g++.exe'; if (!(Test-Path -LiteralPath $hostCxx -PathType Leaf)) { $hostCxx = $clang }
$hostTest = Join-Path $OutputDirectory 'aarch64-phase5-host-tests.exe'
Invoke-Checked $hostCxx @('-std=c++17','-O2',(Join-Path $repoRoot 'tests\aarch64_phase5_host_tests.cpp'),'-o',$hostTest)
& $hostTest; if ($LASTEXITCODE -ne 0) { throw 'Phase 5 host controls failed' }

Write-Host '[8/10] Staging normal App Model packages and negative fixture...' -ForegroundColor Yellow
$stageRoot = Join-Path $OutputDirectory 'ramdisk-root'
$null = New-Item -ItemType Directory -Path (Join-Path $stageRoot 'Apps\Phase5Arm64Proof\bin\arm64') -Force
$null = New-Item -ItemType Directory -Path (Join-Path $stageRoot 'Apps\Phase5WrongMachine\bin\arm64') -Force
$null = New-Item -ItemType Directory -Path (Join-Path $stageRoot 'phase4\nested') -Force
Copy-Item (Join-Path $repoRoot 'sdk\samples\phase5_arm64\app.json') (Join-Path $stageRoot 'Apps\Phase5Arm64Proof\app.json')
Copy-Item (Join-Path $repoRoot 'sdk\samples\phase5_arm64\wrongmachine-app.json') (Join-Path $stageRoot 'Apps\Phase5WrongMachine\app.json')
Copy-Item $appElf (Join-Path $stageRoot 'Apps\Phase5Arm64Proof\bin\arm64\phase5-arm64-proof.elf')
Copy-Item (Join-Path $repoRoot 'Apps\HelloWorld\bin\amd64\helloworld.elf') (Join-Path $stageRoot 'Apps\Phase5WrongMachine\bin\arm64\wrongmachine.elf')
[IO.File]::WriteAllText((Join-Path $stageRoot 'phase4\hello.txt'),'guideXOS AARCH64 Phase 4 filesystem proof',[Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $stageRoot 'phase4\nested\proof.txt'),'guideXOS AARCH64 Phase 4 nested filesystem proof',[Text.Encoding]::ASCII)
$ramdisk = Join-Path $OutputDirectory 'esp\ramdisk.img'
& (Join-Path $PSScriptRoot 'stage-aarch64-phase5-ramdisk.ps1') -InputRoot $stageRoot -OutputImage $ramdisk
if ($LASTEXITCODE -ne 0) { throw 'Phase 5 ramdisk staging failed' }
Copy-Item $efiPath (Join-Path $OutputDirectory 'esp\EFI\BOOT\BOOTAA64.EFI') -Force
Copy-Item $kernelPath (Join-Path $OutputDirectory 'esp\kernel.elf') -Force

Write-Host '[9/10] Recording kernel and application ELF metadata...' -ForegroundColor Yellow
& $llvmReadobj '--file-headers' '--program-headers' '--sections' '--symbols' $appElf | Set-Content (Join-Path $OutputDirectory 'phase5-app-llvm-readobj.txt') -Encoding ascii
& $llvmReadobj '--file-headers' '--program-headers' $kernelPath | Set-Content (Join-Path $OutputDirectory 'kernel-llvm-readobj.txt') -Encoding ascii
if ($LASTEXITCODE -ne 0) { throw 'llvm-readobj failed' }
Write-Host '[10/10] Phase 5 artifacts' -ForegroundColor Yellow
foreach ($artifact in @($efiPath,$kernelPath,$appElf,$ramdisk)) { $item=Get-Item $artifact; Write-Host "      $($item.Name): $($item.Length) bytes sha256=$((Get-FileHash $artifact -Algorithm SHA256).Hash.ToLowerInvariant())" -ForegroundColor Cyan }
Write-Host "      app package: /Apps/Phase5Arm64Proof/bin/arm64/phase5-arm64-proof.elf" -ForegroundColor Cyan
Write-Host 'Build complete. Use scripts\test-aarch64-phase5.ps1 for host controls and three fresh boots.' -ForegroundColor Green
