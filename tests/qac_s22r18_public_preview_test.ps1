$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Import-Module (Join-Path $repoRoot 'scripts\qac-uefi-artifacts.psm1') -Force -ErrorAction Stop
Import-Module (Join-Path $repoRoot 'scripts\qac-s22r10-uefi.psm1') -Force -ErrorAction Stop
$script:checks = 0
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('qac-s22r18-preview ' + [Guid]::NewGuid().ToString('N'))
$sourceRoot = Join-Path $testRoot 'source'
$outputRoot = Join-Path $testRoot 'workspaces'
[void][IO.Directory]::CreateDirectory($sourceRoot)
[void][IO.Directory]::CreateDirectory($outputRoot)

function Assert-True([string]$Name, [bool]$Condition) {
    $script:checks++
    if (-not $Condition) { throw "FAIL [$($script:checks)] $Name" }
    Write-Host "PASS [$($script:checks)] $Name"
}

function Assert-Throws([string]$Name, [scriptblock]$Action, [string]$MessagePattern) {
    $thrown = $false
    $message = ''
    try { & $Action } catch { $thrown = $true; $message = $_.Exception.Message }
    Assert-True $Name ($thrown -and $message -match $MessagePattern)
}

function Write-TestJson([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, (ConvertTo-Json -InputObject $Value -Depth 20) + "`n", (New-Object Text.UTF8Encoding($false)))
}

$isoBytes = New-Object byte[] 32774
[Array]::Copy([Text.Encoding]::ASCII.GetBytes('CD001'), 0, $isoBytes, 32769, 5)
$iso = Join-Path $sourceRoot 'candidate.iso'
[IO.File]::WriteAllBytes($iso, $isoBytes)
$ovmf = Join-Path $sourceRoot 'ovmf.fd'
[IO.File]::WriteAllBytes($ovmf, (New-Object byte[] 4096))
$isoHash = (Get-FileHash -LiteralPath $iso -Algorithm SHA256).Hash
$ovmfHash = (Get-FileHash -LiteralPath $ovmf -Algorithm SHA256).Hash
$prepare = Join-Path $repoRoot 'scripts\prepare-qac-s22r18-public-preview.ps1'
$preparedText = & $prepare -IsoPath $iso -OvmfPath $ovmf -RunId 'synthetic-preview' -OutputRoot $outputRoot -ExpectedIsoSha256 $isoHash -ExpectedOvmfSha256 $ovmfHash
$prepared = ($preparedText -join "`n") | ConvertFrom-Json
$manifestPath = [string]$prepared.manifestPath

try {
    $validated = Get-QacS22R10ValidatedManifest -ManifestPath $manifestPath -PublicPreviewIsoOnly
    Assert-True 'public-preview staging contains exactly read-only ISO and firmware artifacts with no ESP' (@($validated.Manifest.artifacts).Count -eq 2 -and -not $validated.Esp -and @($validated.Manifest.artifacts | Where-Object { $_.access -ne 'read-only' -or $_.disposition -ne 'share' }).Count -eq 0)

    $qmpPort = Get-QacS22R10QmpPortCandidate
    $qemuArgs = New-QacS22R10QemuArguments -ValidatedManifest $validated -QemuPath 'C:\Program Files\qemu\qemu-system-x86_64.exe' -QmpPort $qmpPort
    $driveIndexes = @(); for ($i = 0; $i -lt $qemuArgs.Count; $i++) { if ($qemuArgs[$i] -eq '-drive') { $driveIndexes += $i } }
    $driveArguments = @($driveIndexes | ForEach-Object { $qemuArgs[$_ + 1] })
    $joined = $qemuArgs -join ' '
    Assert-True 'launch has exactly two read-only firmware and CD-ROM drives' ($driveArguments.Count -eq 2 -and $driveArguments[0] -match 'readonly=on' -and $driveArguments[1] -match 'media=cdrom,readonly=on')
    Assert-True 'launch disables networking, HMP, writable disks, and host filesystem sharing' ($qemuArgs -contains '-net' -and $qemuArgs -contains 'none' -and $qemuArgs -contains '-monitor' -and $qemuArgs -contains 'none' -and $joined -notmatch '(?i)fat:rw:|virtio-9p|virtiofs|vhost-user|smb=|hostfwd=')
    Assert-True 'launch QEMU arguments retain the exact verified ISO and read-only OVMF paths' ($joined.Contains($iso) -and $joined.Contains($ovmf) -and $joined -match '(?i)if=pflash,format=raw,readonly=on,file=')
    Assert-Throws 'public-preview profile rejects a guest-accessible COM2 host channel' { New-QacS22R10QemuArguments -ValidatedManifest $validated -QemuPath 'qemu-system-x86_64.exe' -QmpPort $qmpPort -Com2TcpPort (Get-QacS22R10QmpPortCandidate -ExcludePort @($qmpPort)) } 'forbids guest-accessible COM2'

    $manifest = $validated.Manifest
    $manifest.launchContract.arguments += @('-drive', 'if=ide,file=host-share.img,format=raw')
    Write-TestJson $manifestPath $manifest
    $tampered = Get-QacS22R10ValidatedManifest -ManifestPath $manifestPath -PublicPreviewIsoOnly
    Assert-Throws 'tampered launch contract with a writable guest disk is rejected before QAC launch' { New-QacS22R10QemuArguments -ValidatedManifest $tampered -QemuPath 'qemu-system-x86_64.exe' -QmpPort $qmpPort } 'only read-only firmware and ISO drives'

    Write-Host "S22R18 QAC profile tests passed: $script:checks assertions."
} finally {
    if (Test-Path -LiteralPath $testRoot -PathType Container) {
        $resolvedTestRoot = [IO.Path]::GetFullPath($testRoot)
        $resolvedTempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
        if (-not $resolvedTestRoot.StartsWith($resolvedTempRoot, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove fixture path outside the temp root: $resolvedTestRoot"
        }
        Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
    }
}
