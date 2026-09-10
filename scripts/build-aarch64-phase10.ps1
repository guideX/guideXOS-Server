[CmdletBinding()]
param(
    [string]$LlvmRoot = 'C:\Program Files\LLVM\bin',
    [string]$OutputDirectory = '',
    [switch]$Phase11Proof
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) { $OutputDirectory = Join-Path $repoRoot 'out\aarch64-phase10' }
else { $OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory) }
$rootPrefix = $repoRoot.TrimEnd('\') + '\out\'
if (!$OutputDirectory.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) { throw "Refusing output outside repository out/: $OutputDirectory" }

$clang = Join-Path $LlvmRoot 'clang++.exe'
$ld = Join-Path $LlvmRoot 'ld.lld.exe'
$lldLink = Join-Path $LlvmRoot 'lld-link.exe'
$llvmReadobj = Join-Path $LlvmRoot 'llvm-readobj.exe'
foreach ($tool in @($clang, $ld, $lldLink, $llvmReadobj)) {
    if (!(Test-Path -LiteralPath $tool -PathType Leaf)) { throw "Required LLVM tool not found: $tool" }
}
function Invoke-Checked { param([string]$FilePath, [string[]]$Arguments)
    Write-Host "      $([IO.Path]::GetFileName($FilePath)) $($Arguments -join ' ')" -ForegroundColor DarkGray
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Command failed ($LASTEXITCODE): $FilePath" }
}
function Read-U16([byte[]]$Bytes, [int]$Offset) { return [int]$Bytes[$Offset] -bor ([int]$Bytes[$Offset + 1] -shl 8) }
function Read-U32([byte[]]$Bytes, [int]$Offset) {
    return [uint32]$Bytes[$Offset] -bor ([uint32]$Bytes[$Offset + 1] -shl 8) -bor
        ([uint32]$Bytes[$Offset + 2] -shl 16) -bor ([uint32]$Bytes[$Offset + 3] -shl 24)
}
function Test-PeArm64([string]$Path) {
    $b = [IO.File]::ReadAllBytes($Path)
    if ($b.Length -lt 64) { return $false }
    $p = [int](Read-U32 $b 60)
    return $p -ge 0 -and $p + 6 -le $b.Length -and $b[$p] -eq 0x50 -and $b[$p + 1] -eq 0x45 -and (Read-U16 $b ($p + 4)) -eq 0xaa64
}
function Test-Elf([string]$Path, [int]$Machine) {
    $b = [IO.File]::ReadAllBytes($Path)
    return $b.Length -ge 64 -and $b[0] -eq 0x7f -and $b[1] -eq 0x45 -and $b[2] -eq 0x4c -and $b[3] -eq 0x46 -and
        $b[4] -eq 2 -and $b[5] -eq 1 -and $b[6] -eq 1 -and (Read-U16 $b 18) -eq $Machine
}

if (Test-Path -LiteralPath $OutputDirectory) { Remove-Item -LiteralPath $OutputDirectory -Recurse -Force }
$null = New-Item -ItemType Directory -Path (Join-Path $OutputDirectory 'esp\EFI\BOOT') -Force

$loaderObject = Join-Path $OutputDirectory 'phase10_loader.obj'
$efiPath = Join-Path $OutputDirectory 'BOOTAA64.EFI'
$loaderSource = Join-Path $repoRoot 'guideXOSBootLoader\aarch64\phase1_loader.cpp'
$phase11Define = if ($Phase11Proof) { '-DGXOS_AARCH64_PHASE11' } else { }
$loaderFlags = @(
    '--target=aarch64-pc-windows-msvc', '-DGXOS_AARCH64_PHASE4', '-DGXOS_AARCH64_PHASE5', '-DGXOS_AARCH64_PHASE6', '-DGXOS_AARCH64_PHASE7', '-DGXOS_AARCH64_PHASE8', '-DGXOS_AARCH64_PHASE9', '-DGXOS_AARCH64_PHASE10',
    '-O2', '-ffreestanding', '-fno-builtin', '-fno-stack-protector', '-fno-exceptions', '-fno-rtti', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables', '-fno-ident',
    '-I', (Join-Path $repoRoot 'guideXOSBootLoader'), '-I', $repoRoot, '-c', $loaderSource, '-o', $loaderObject)
Write-Host '[1/12] Compiling ARM64 UEFI loader...' -ForegroundColor Yellow
Invoke-Checked $clang $loaderFlags
Invoke-Checked $lldLink @('/subsystem:efi_application','/entry:efi_main','/nodefaultlib','/machine:arm64','/timestamp:0',"/out:$efiPath",$loaderObject)
if (!(Test-PeArm64 $efiPath)) { throw 'BOOTAA64.EFI failed ARM64 PE verification' }

$kernelSources = @(
    'kernel\arch\arm64\phase3_entry.S','kernel\arch\arm64\phase3_context.S','kernel\arch\arm64\phase3_register_test.S',
    'kernel\arch\arm64\phase3_preemptive_worker.S','kernel\arch\arm64\native_elf_call.S',
    'kernel\arch\arm64\phase3_serial.cpp','kernel\arch\arm64\phase4_serial.cpp','kernel\arch\arm64\serial_console.cpp',
    'kernel\arch\arm64\phase2_platform.cpp','kernel\arch\arm64\phase2_mmu.cpp','kernel\arch\arm64\phase5_mmu_permissions.cpp',
    'kernel\arch\arm64\phase2_gic.cpp','kernel\arch\arm64\phase3_timer.cpp','kernel\arch\arm64\phase3_arch.cpp',
    'kernel\arch\arm64\phase3_exceptions.cpp','kernel\arch\arm64\phase4_main.cpp','kernel\arch\arm64\phase6_platform_stubs.cpp',
    'kernel\core\common_scheduler.cpp','kernel\core\common_kernel_entry.cpp','kernel\core\common_boot_info.cpp',
    'kernel\core\common_physical_allocator.cpp','kernel\core\application_event_service.cpp','kernel\core\application_runtime.cpp',
    'kernel\core\irq_registry.cpp','kernel\core\cxx_runtime.cpp','kernel\core\block_device.cpp','kernel\core\ramdisk.cpp',
    'kernel\core\vfs.cpp','kernel\core\fs_fat.cpp','kernel\core\fs_ext4.cpp','kernel\core\native_architecture.cpp',
    'kernel\core\native_elf_baremetal.cpp','kernel\core\compiler\compiler_target.cpp','kernel\core\compiler\compiler_diagnostics.cpp','kernel\core\compiler\compiler_lexer.cpp','kernel\core\compiler\compiler_parser.cpp','kernel\core\compiler\compiler_driver.cpp','kernel\core\compiler\compiler_build_service.cpp','kernel\core\compiler\elf_writer.cpp','kernel\arch\amd64\compiler_backend.cpp','kernel\arch\arm64\compiler_backend.cpp','kernel\core\framebuffer.cpp','kernel\core\desktop.cpp','kernel\core\desktop_font.cpp',
    'kernel\core\system_font.cpp','kernel\core\kernel_compositor.cpp','kernel\core\kernel_ipc.cpp','kernel\core\kernel_app.cpp',
    'kernel\core\input_queue.cpp','kernel\core\input_manager.cpp','kernel\core\display_input_mapper.cpp','kernel\core\virtio_input.cpp',
    'kernel\core\file_clipboard.cpp','kernel\core\desktop_capabilities.cpp','kernel\core\time.cpp','kernel\core\image_adapter.cpp')
$kernelFlags = @(
    '--target=aarch64-none-elf','-std=c++14','-march=armv8-a','-O2','-ffreestanding','-nostdlib','-nostdinc++','-fno-builtin','-fno-stack-protector',
    '-fno-exceptions','-fno-rtti','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-fno-pic','-fno-pie','-mcmodel=small',
    '-mgeneral-regs-only','-mstrict-align','-mno-outline-atomics','-ffunction-sections','-fdata-sections','-Wno-c++11-narrowing',
    '-DGXOS_AARCH64_PHASE4','-DGXOS_AARCH64_PHASE5','-DGXOS_AARCH64_PHASE6','-DGXOS_AARCH64_PHASE7','-DGXOS_AARCH64_PHASE8','-DGXOS_AARCH64_PHASE9','-DGXOS_AARCH64_PHASE10',$phase11Define,
    '-DGXOS_BARE_METAL','-DKERNEL_HAS_VIRTIO_INPUT','-DKERNEL_HAS_COMMON_INPUT_QUEUE','-I',$repoRoot,'-I',(Join-Path $repoRoot 'kernel'),'-I',(Join-Path $repoRoot 'sdk\include'),'-I',(Join-Path $repoRoot 'kernel\core\include'),
    '-I',(Join-Path $repoRoot 'kernel\core\freestanding'),'-I',(Join-Path $repoRoot 'kernel\arch\arm64\include'))
$kernelObjects = @()
$kernelSourceIndex = 0
Write-Host '[2/12] Compiling common runtime, graphics, and ARM64 sources...' -ForegroundColor Yellow
foreach ($relative in $kernelSources) {
    $source = Join-Path $repoRoot $relative
    $object = Join-Path $OutputDirectory ('kernel-{0:D3}.o' -f $kernelSourceIndex)
    ++$kernelSourceIndex
    Invoke-Checked $clang ($kernelFlags + @('-c',$source,'-o',$object))
    $kernelObjects += $object
}
$kernelPath = Join-Path $OutputDirectory 'kernel.elf'
Write-Host '[3/12] Linking Phase 10 kernel...' -ForegroundColor Yellow
Invoke-Checked $ld (@('-m','aarch64elf','-T',(Join-Path $repoRoot 'kernel\arch\arm64\phase3_linker.ld'),'--gc-sections','-o',$kernelPath) + $kernelObjects)
if (!(Test-Elf $kernelPath 183)) { throw 'kernel.elf failed EM_AARCH64 verification' }

$appSource = Join-Path $repoRoot 'sdk\samples\phase10_multiarch_proof\main.cpp'
$armObject = Join-Path $OutputDirectory 'phase10-multiarch-proof-arm64.o'
$armElf = Join-Path $OutputDirectory 'phase10-multiarch-proof-arm64.elf'
$armFlags = @('--target=aarch64-none-elf','-std=c++14','-march=armv8-a','-O2','-ffreestanding','-nostdlib','-nostdinc++','-fno-builtin','-fno-exceptions','-fno-rtti','-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-fno-pic','-fno-pie','-mcmodel=small','-mgeneral-regs-only','-mstrict-align','-I',(Join-Path $repoRoot 'sdk\include'),'-c',$appSource,'-o',$armObject)
Write-Host '[4/12] Compiling ARM64 payload from shared proof source...' -ForegroundColor Yellow
Invoke-Checked $clang $armFlags
Invoke-Checked $ld @('-m','aarch64elf','--image-base=0x50000000','-z','max-page-size=0x1000','-e','gx_main','-o',$armElf,$armObject)
if (!(Test-Elf $armElf 183)) { throw 'ARM64 payload failed ELF64 EM_AARCH64 verification' }

$hostCxx = 'C:\mingw64\bin\g++.exe'; if (!(Test-Path -LiteralPath $hostCxx -PathType Leaf)) { $hostCxx = 'g++.exe' }
$amdObject = Join-Path $OutputDirectory 'phase10-multiarch-proof-amd64.o'
$amdElf = Join-Path $OutputDirectory 'phase10-multiarch-proof-amd64.elf'
$amdFlags = @('--target=x86_64-none-elf','-std=c++14','-O2','-ffreestanding','-nostdlib','-nostdinc++','-fno-builtin','-fno-exceptions','-fno-rtti','-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-fno-pic','-fno-pie','-mcmodel=small','-mno-red-zone','-I',(Join-Path $repoRoot 'sdk\include'),'-c',$appSource,'-o',$amdObject)
Write-Host '[5/12] Independently compiling genuine AMD64 payload from shared proof source...' -ForegroundColor Yellow
Invoke-Checked $clang $amdFlags
Invoke-Checked $ld @('-m','elf_x86_64','--image-base=0x50000000','-z','max-page-size=0x1000','-e','gx_main','-o',$amdElf,$amdObject)
if (!(Test-Elf $amdElf 62)) { throw 'AMD64 payload failed ELF64 EM_X86_64 verification' }

$controlsTest = Join-Path $OutputDirectory 'aarch64-phase10-host-tests.exe'
$abiTest = Join-Path $OutputDirectory 'native-abi-layout-test.exe'
Write-Host '[6/12] Running common resolver, ELF, discovery, ABI, and AMD64 compile controls...' -ForegroundColor Yellow
Invoke-Checked $hostCxx @('-std=c++17','-Wall','-Wextra','-O2','-iquote',$repoRoot,
    (Join-Path $repoRoot 'tests\aarch64_phase10_host_tests.cpp'),(Join-Path $repoRoot 'app_manifest.cpp'),(Join-Path $repoRoot 'app_manifest_loader.cpp'),
    (Join-Path $repoRoot 'app_manifest_validator.cpp'),(Join-Path $repoRoot 'app_payload_resolver.cpp'),(Join-Path $repoRoot 'app_registry.cpp'),
    (Join-Path $repoRoot 'kernel\core\native_architecture.cpp'),(Join-Path $repoRoot 'elf_validator.cpp'),'-o',$controlsTest)
Invoke-Checked $controlsTest @()
Invoke-Checked $hostCxx @('-std=c++17','-Wall','-Wextra','-O2','-idirafter',$repoRoot,'-idirafter',(Join-Path $repoRoot 'sdk\include'),(Join-Path $repoRoot 'tests\native_abi_layout_test.cpp'),'-o',$abiTest)
Invoke-Checked $abiTest @()
Invoke-Checked $hostCxx @('-std=c++17','-m64','-O2','-fno-builtin','-fno-exceptions','-fno-rtti','-iquote',$repoRoot,'-c',(Join-Path $repoRoot 'app_launch_resolver.cpp'),'-o',(Join-Path $OutputDirectory 'app-launch-resolver-amd64.o'))

Write-Host '[7/12] Staging one shared-identity package with two native payloads...' -ForegroundColor Yellow
$stageRoot = Join-Path $OutputDirectory 'ramdisk-root'
$packageRoot = Join-Path $stageRoot 'Apps\Phase10MultiArchProof'
$null = New-Item -ItemType Directory -Path (Join-Path $packageRoot 'bin\amd64') -Force
$null = New-Item -ItemType Directory -Path (Join-Path $packageRoot 'bin\arm64') -Force
$null = New-Item -ItemType Directory -Path (Join-Path $stageRoot 'phase4\nested') -Force
$null = New-Item -ItemType Directory -Path (Join-Path $stageRoot 'wall') -Force
Copy-Item (Join-Path $repoRoot 'sdk\samples\phase10_multiarch_proof\app.json') (Join-Path $packageRoot 'app.json')
Copy-Item $amdElf (Join-Path $packageRoot 'bin\amd64\phase10-multiarch-proof.elf')
Copy-Item $armElf (Join-Path $packageRoot 'bin\arm64\phase10-multiarch-proof.elf')
[IO.File]::WriteAllText((Join-Path $stageRoot 'phase4\hello.txt'),'guideXOS AARCH64 Phase 4 filesystem proof',[Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $stageRoot 'phase4\nested\proof.txt'),'guideXOS AARCH64 Phase 4 nested filesystem proof',[Text.Encoding]::ASCII)
$wallpaper = Join-Path $repoRoot 'out\wallpaper-pack\wall\blueflwr.gxi'
if (!(Test-Path -LiteralPath $wallpaper -PathType Leaf)) { throw "Normal desktop wallpaper resource is missing: $wallpaper" }
Copy-Item $wallpaper (Join-Path $stageRoot 'wall\blueflwr.gxi')
$ramdisk = Join-Path $OutputDirectory 'esp\ramdisk.img'
& (Join-Path $PSScriptRoot 'stage-aarch64-phase5-ramdisk.ps1') -InputRoot $stageRoot -OutputImage $ramdisk
if ($LASTEXITCODE -ne 0) { throw 'Phase 10 FAT32 ramdisk staging failed' }
Copy-Item $efiPath (Join-Path $OutputDirectory 'esp\EFI\BOOT\BOOTAA64.EFI') -Force
Copy-Item $kernelPath (Join-Path $OutputDirectory 'esp\kernel.elf') -Force

Write-Host '[8/12] Recording payload and kernel metadata...' -ForegroundColor Yellow
& $llvmReadobj '--file-headers' '--program-headers' '--sections' '--symbols' $armElf | Set-Content (Join-Path $OutputDirectory 'phase10-arm64-llvm-readobj.txt') -Encoding ascii
& $llvmReadobj '--file-headers' '--program-headers' '--sections' '--symbols' $amdElf | Set-Content (Join-Path $OutputDirectory 'phase10-amd64-llvm-readobj.txt') -Encoding ascii
& $llvmReadobj '--file-headers' '--program-headers' $kernelPath | Set-Content (Join-Path $OutputDirectory 'kernel-llvm-readobj.txt') -Encoding ascii
if ($LASTEXITCODE -ne 0) { throw 'llvm-readobj failed' }
Write-Host '[9/12] Verifying staged identity and both machine headers...' -ForegroundColor Yellow
$stagedManifest = Join-Path $packageRoot 'app.json'
if (!(Test-Path -LiteralPath $stagedManifest -PathType Leaf) -or !(Test-Elf (Join-Path $packageRoot 'bin\arm64\phase10-multiarch-proof.elf') 183) -or
    !(Test-Elf (Join-Path $packageRoot 'bin\amd64\phase10-multiarch-proof.elf') 62)) { throw 'Staged Phase 10 package verification failed' }
Write-Host '[10/12] Computing artifact details...' -ForegroundColor Yellow
foreach ($artifact in @($efiPath,$kernelPath,$armElf,$amdElf,$ramdisk)) {
    $item = Get-Item -LiteralPath $artifact
    Write-Host "      $($item.Name): $($item.Length) bytes sha256=$((Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash.ToLowerInvariant())" -ForegroundColor Cyan
}
Write-Host '[11/12] Package layout:' -ForegroundColor Yellow
Write-Host '      Apps/Phase10MultiArchProof/app.json' -ForegroundColor Cyan
Write-Host '      Apps/Phase10MultiArchProof/bin/amd64/phase10-multiarch-proof.elf (EM_X86_64)' -ForegroundColor Cyan
Write-Host '      Apps/Phase10MultiArchProof/bin/arm64/phase10-multiarch-proof.elf (EM_AARCH64)' -ForegroundColor Cyan
Write-Host '[12/12] Phase 10 build complete.' -ForegroundColor Green
