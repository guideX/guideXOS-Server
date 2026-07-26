param(
    [string]$Workspace = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path,
    [string]$EvidenceRoot = '',
    [int]$Runs = 3,
    [int]$TimeoutSeconds = 30
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path $Workspace 'out/dotnet/gc-initialization-dry-run/qemu'
}

$qemu = 'C:\Program Files\qemu\qemu-system-x86_64.exe'
$ovmf = 'C:\Program Files\qemu\share\edk2-x86_64-code.fd'
$bootloader = Join-Path $Workspace 'guideXOSBootLoader/x64/Release/guideXOSBootLoader.exe'
$kernel = Join-Path $Workspace 'kernel/build/amd64/bin/kernel.elf'
$artifact = Join-Path $Workspace 'out/dotnet/gc-initialization-dry-run/artifact/NativeAotGcStartupMinimal.exe'

foreach ($path in @($qemu, $ovmf, $bootloader, $kernel, $artifact)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required QEMU startup input is missing: $path"
    }
}

New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null
$labels = @('first', 'repeat', 'fresh')
if ($Runs -lt 1 -or $Runs -gt $labels.Count) {
    throw 'Runs must be between 1 and 3.'
}

$results = @()
for ($index = 0; $index -lt $Runs; ++$index) {
    $label = $labels[$index]
    $runRoot = Join-Path $EvidenceRoot $label
    $espRoot = Join-Path $runRoot 'esp'
    $bootPath = Join-Path $espRoot 'EFI/BOOT/BOOTX64.EFI'
    $serial = Join-Path $runRoot 'serial.log'
    New-Item -ItemType Directory -Force -Path (Split-Path $bootPath) | Out-Null
    Copy-Item -LiteralPath $bootloader -Destination $bootPath -Force
    Copy-Item -LiteralPath $kernel -Destination (Join-Path $espRoot 'kernel.elf') -Force

    $qemuArgs = @(
        '-accel', 'tcg,thread=single', '-machine', 'pc', '-smp', '1',
        '-drive', ('if=pflash,format=raw,readonly=on,file="' + $ovmf + '"'),
        '-drive', ('file=fat:rw:"' + $espRoot + '",format=raw,if=ide,index=0'),
        '-m', '1024M', '-vga', 'std', '-display', 'none',
        '-serial', ('file:"' + $serial + '"'), '-no-reboot', '-no-shutdown',
        '-rtc', 'base=utc,clock=host'
    )
    $process = Start-Process -FilePath $qemu -ArgumentList $qemuArgs -WindowStyle Hidden -PassThru
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $marker = $false
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 250
        if (Test-Path -LiteralPath $serial) {
            $text = Get-Content -LiteralPath $serial -Raw
            if ($text -match '\[nativeaot-gc-startup-qemu-test\] ALL_(PASS|FAIL)') {
                $marker = $true
                break
            }
        }
    }
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
    }
    $serialText = if (Test-Path -LiteralPath $serial) {
        Get-Content -LiteralPath $serial -Raw
    } else { '' }
    $pass = $marker -and $serialText -match '\[nativeaot-gc-startup-qemu-test\] ALL_PASS'
    [pscustomobject]@{
        label = $label
        process = 'disposable'
        marker = $marker
        pass = $pass
        serial = $serial
    } | Tee-Object -Variable result | Out-Null
    $results += $result
}

$manifest = [ordered]@{
    qemuLocated = $true
    artifact = $artifact
    kernel = $kernel
    bootloader = $bootloader
    sameProcessShutdown = 'unsupported-by-locked-NativeAOT-contract'
    runs = $results
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $EvidenceRoot 'manifest.json')
$results | Format-Table -AutoSize | Out-String | Set-Content -LiteralPath (Join-Path $EvidenceRoot 'matrix.txt')
$results
if (@($results | Where-Object { -not $_.pass }).Count -ne 0) { exit 1 }
