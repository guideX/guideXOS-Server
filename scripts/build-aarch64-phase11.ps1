[CmdletBinding()]
param(
    [string]$LlvmRoot = 'C:\Program Files\LLVM\bin',
    [string]$OutputDirectory = '',
    [switch]$SkipDeveloperStudio
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) { $OutputDirectory = Join-Path $repoRoot 'out\aarch64-phase11' }
else { $OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory) }
$rootPrefix = $repoRoot.TrimEnd('\') + '\out\'
if (!$OutputDirectory.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) { throw "Refusing output outside repository out/: $OutputDirectory" }

function Read-U16([byte[]]$Bytes, [int]$Offset) { return [int]$Bytes[$Offset] -bor ([int]$Bytes[$Offset + 1] -shl 8) }
function Test-Elf([string]$Path, [int]$Machine) {
    $b = [IO.File]::ReadAllBytes($Path)
    return $b.Length -ge 64 -and $b[0] -eq 0x7f -and $b[1] -eq 0x45 -and $b[2] -eq 0x4c -and $b[3] -eq 0x46 -and
        $b[4] -eq 2 -and $b[5] -eq 1 -and $b[6] -eq 1 -and (Read-U16 $b 18) -eq $Machine
}
function Invoke-Checked { param([string]$FilePath, [string[]]$Arguments)
    Write-Host "      $([IO.Path]::GetFileName($FilePath)) $($Arguments -join ' ')" -ForegroundColor DarkGray
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Command failed ($LASTEXITCODE): $FilePath" }
}

$phase10Script = Join-Path $PSScriptRoot 'build-aarch64-phase10.ps1'
Write-Host '[1/6] Building the Phase-10 kernel foundation with the resident Phase-11 path enabled...' -ForegroundColor Yellow
& $phase10Script -LlvmRoot $LlvmRoot -OutputDirectory $OutputDirectory -Phase11Proof
if ($LASTEXITCODE -ne 0) { throw 'Phase-10 foundation build failed' }

$clang = Join-Path $LlvmRoot 'clang++.exe'
$ld = Join-Path $LlvmRoot 'ld.lld.exe'
foreach ($tool in @($clang, $ld)) { if (!(Test-Path -LiteralPath $tool -PathType Leaf)) { throw "Required LLVM tool not found: $tool" } }

$dsElf = Join-Path $OutputDirectory 'phase11-developer-studio-arm64.elf'
if (!$SkipDeveloperStudio) {
    $dsSource = Join-Path $repoRoot 'sdk\samples\phase11_developer_studio\main.cpp'
    $dsObject = Join-Path $OutputDirectory 'phase11-developer-studio-arm64.o'
    $dsFlags = @('--target=aarch64-none-elf','-std=c++14','-march=armv8-a','-O2','-ffreestanding','-nostdlib','-nostdinc++','-fno-builtin','-fno-exceptions','-fno-rtti','-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-fno-pic','-fno-pie','-mcmodel=small','-mgeneral-regs-only','-mstrict-align','-I',(Join-Path $repoRoot 'sdk\include'),'-c',$dsSource,'-o',$dsObject)
    Write-Host '[2/6] Compiling the checked-in Developer Studio build client for ARM64...' -ForegroundColor Yellow
    Invoke-Checked $clang $dsFlags
    Invoke-Checked $ld @('-m','aarch64elf','--image-base=0x50000000','-z','max-page-size=0x1000','-e','gx_main','-o',$dsElf,$dsObject)
    if (!(Test-Elf $dsElf 183)) { throw 'Developer Studio build client failed EM_AARCH64 verification' }
} else {
    Write-Host '[2/6] Skipping the Developer Studio client by request.' -ForegroundColor DarkGray
}

$stageRoot = Join-Path $OutputDirectory 'ramdisk-root'
if (Test-Path -LiteralPath $stageRoot) { Remove-Item -LiteralPath $stageRoot -Recurse -Force }
$packageRoot = Join-Path $stageRoot 'Apps\Phase11CrossArchApp'
$developerStudioRoot = Join-Path $stageRoot 'Apps\DeveloperStudioPhase11'
$null = New-Item -ItemType Directory -Path (Join-Path $packageRoot 'src') -Force
$null = New-Item -ItemType Directory -Path (Join-Path $packageRoot 'bin\amd64') -Force
Copy-Item -LiteralPath (Join-Path $repoRoot 'sdk\samples\phase11_crossarch_app\app\app.json') -Destination (Join-Path $packageRoot 'app.json') -Force
Copy-Item -LiteralPath (Join-Path $repoRoot 'sdk\samples\phase11_crossarch_app\guidexos.project') -Destination (Join-Path $packageRoot 'guidexos.project') -Force
Copy-Item -LiteralPath (Join-Path $repoRoot 'sdk\samples\phase11_crossarch_app\src\main.c') -Destination (Join-Path $packageRoot 'src\main.c') -Force

$companionObject = Join-Path $OutputDirectory 'phase11-companion-amd64.o'
$companionElf = Join-Path $OutputDirectory 'phase11-companion-amd64.elf'
$companionFlags = @('--target=x86_64-none-elf','-std=c++14','-O2','-ffreestanding','-nostdlib','-nostdinc++','-fno-builtin','-fno-exceptions','-fno-rtti','-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-fno-pic','-fno-pie','-mcmodel=small','-mno-red-zone','-I',(Join-Path $repoRoot 'sdk\include'),'-c',(Join-Path $repoRoot 'sdk\samples\phase11_crossarch_app\companion.cpp'),'-o',$companionObject)
Write-Host '[3/6] Building the staged AMD64 control payload used before guest publication...' -ForegroundColor Yellow
Invoke-Checked $clang $companionFlags
Invoke-Checked $ld @('-m','elf_x86_64','--image-base=0x50000000','-z','max-page-size=0x1000','-e','gx_main','-o',$companionElf,$companionObject)
if (!(Test-Elf $companionElf 62)) { throw 'Phase-11 AMD64 control payload failed EM_X86_64 verification' }
Copy-Item -LiteralPath $companionElf -Destination (Join-Path $packageRoot 'bin\amd64\app.elf') -Force

if (!$SkipDeveloperStudio) {
    $null = New-Item -ItemType Directory -Path (Join-Path $developerStudioRoot 'bin\arm64') -Force
    Copy-Item -LiteralPath (Join-Path $repoRoot 'sdk\samples\phase11_developer_studio\app.json') -Destination (Join-Path $developerStudioRoot 'app.json') -Force
    Copy-Item -LiteralPath $dsElf -Destination (Join-Path $developerStudioRoot 'bin\arm64\developerstudio.elf') -Force
}

Write-Host '[4/6] Adding the normal filesystem and wallpaper fixtures...' -ForegroundColor Yellow
$null = New-Item -ItemType Directory -Path (Join-Path $stageRoot 'phase4\nested') -Force
$null = New-Item -ItemType Directory -Path (Join-Path $stageRoot 'wall') -Force
[IO.File]::WriteAllText((Join-Path $stageRoot 'phase4\hello.txt'),'guideXOS AARCH64 Phase 4 filesystem proof',[Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $stageRoot 'phase4\nested\proof.txt'),'guideXOS AARCH64 Phase 4 nested filesystem proof',[Text.Encoding]::ASCII)
$wallpaper = Join-Path $repoRoot 'out\wallpaper-pack\wall\blueflwr.gxi'
if (!(Test-Path -LiteralPath $wallpaper -PathType Leaf)) { throw "Normal desktop wallpaper resource is missing: $wallpaper" }
Copy-Item -LiteralPath $wallpaper -Destination (Join-Path $stageRoot 'wall\blueflwr.gxi') -Force

Write-Host '[5/6] Staging the Phase-11 FAT32 ramdisk...' -ForegroundColor Yellow
$ramdisk = Join-Path $OutputDirectory 'esp\ramdisk.img'
& (Join-Path $PSScriptRoot 'stage-aarch64-phase5-ramdisk.ps1') -InputRoot $stageRoot -OutputImage $ramdisk
if ($LASTEXITCODE -ne 0) { throw 'Phase-11 FAT32 ramdisk staging failed' }
if (!(Test-Elf (Join-Path $packageRoot 'bin\amd64\app.elf') 62)) { throw 'Staged Phase-11 AMD64 control payload failed verification' }
if (!$SkipDeveloperStudio -and !(Test-Elf (Join-Path $developerStudioRoot 'bin\arm64\developerstudio.elf') 183)) { throw 'Staged Developer Studio payload failed verification' }
if (Test-Path -LiteralPath (Join-Path $packageRoot 'bin\arm64\app.elf')) { throw 'Phase-11 ARM64 proof payload was pre-staged' }

Write-Host '[6/6] Phase-11 build complete.' -ForegroundColor Green
Write-Host '      Resident compiler: kernel/compiler + checked-in Developer Studio client.' -ForegroundColor Cyan
Write-Host '      Guest-generated package: Apps/Phase11CrossArchApp/bin/{arm64,amd64}/app.elf' -ForegroundColor Cyan
Write-Host '      Staged control payload: Apps/Phase11CrossArchApp/bin/amd64/app.elf (EM_X86_64)' -ForegroundColor Cyan
