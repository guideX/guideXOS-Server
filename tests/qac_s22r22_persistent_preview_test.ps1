$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Import-Module (Join-Path $repoRoot 'scripts\qac-uefi-artifacts.psm1') -Force -ErrorAction Stop
Import-Module (Join-Path $repoRoot 'scripts\qac-s22r10-uefi.psm1') -Force -ErrorAction Stop

$script:checks = 0
$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('qac-s22r22-preview ' + [Guid]::NewGuid().ToString('N'))
$dataRoot = 'E:\CodexArtifacts\S22R22\persistent-data'
$dataPath = Join-Path $dataRoot ('qac-profile-fixture-' + [Guid]::NewGuid().ToString('N') + '.img')
if (-not (Test-Path -LiteralPath $dataRoot -PathType Container)) {
    throw "Dedicated S22R22 data-volume root is unavailable: $dataRoot"
}
[void][IO.Directory]::CreateDirectory($fixtureRoot)

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
    [IO.File]::WriteAllText($Path, (ConvertTo-Json -InputObject $Value -Depth 24) + "`n", (New-Object Text.UTF8Encoding($false)))
}

try {
    $isoBytes = New-Object byte[] 32774
    [Array]::Copy([Text.Encoding]::ASCII.GetBytes('CD001'), 0, $isoBytes, 32769, 5)
    $isoPath = Join-Path $fixtureRoot 'candidate.iso'
    [IO.File]::WriteAllBytes($isoPath, $isoBytes)
    $ovmfPath = Join-Path $fixtureRoot 'ovmf.fd'
    [IO.File]::WriteAllBytes($ovmfPath, (New-Object byte[] 4096))
    $stream = [IO.File]::Open($dataPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $stream.SetLength(64MB) } finally { $stream.Dispose() }

    $isoHash = (Get-FileHash -LiteralPath $isoPath -Algorithm SHA256).Hash
    $ovmfHash = (Get-FileHash -LiteralPath $ovmfPath -Algorithm SHA256).Hash
    $dataHash = (Get-FileHash -LiteralPath $dataPath -Algorithm SHA256).Hash
    $prepare = Join-Path $repoRoot 'scripts\prepare-qac-s22r22-persistent-preview.ps1'
    $preparedText = & $prepare -IsoPath $isoPath -OvmfPath $ovmfPath -DataImagePath $dataPath `
        -DataVolumeRoot $dataRoot -RunId 'synthetic-s22r22-preview' -OutputRoot (Join-Path $fixtureRoot 'workspaces') `
        -ExpectedIsoSha256 $isoHash -ExpectedOvmfSha256 $ovmfHash -ExpectedDataImageSha256 $dataHash
    $prepared = ($preparedText -join "`n") | ConvertFrom-Json
    $manifestPath = [string]$prepared.manifestPath

    $validated = Get-QacS22R10ValidatedManifest -ManifestPath $manifestPath -PublicPreviewIsoOnly `
        -PersistentDataVolume -ExpectedDataImagePath $dataPath -ExpectedDataVolumeRoot $dataRoot `
        -ExpectedDataImageSha256 $dataHash
    $originalArguments = @($validated.Manifest.launchContract.arguments)
    Assert-True 'S22R22 manifest shares only read-only ISO and OVMF artifacts' `
        (@($validated.Manifest.artifacts).Count -eq 2 -and @($validated.Manifest.artifacts | Where-Object { $_.access -ne 'read-only' -or $_.disposition -ne 'share' }).Count -eq 0)
    Assert-True 'one external data image has the stable identity, raw format and reuse contract' `
        (@($validated.Manifest.launchContract.storage.writableGuestDisks).Count -eq 1 -and
         $validated.DataVolume.stableAtaSerial -ceq 'GXOS-DATA-V1' -and
         $validated.DataVolume.qemuFormat -ceq 'raw' -and
         $validated.DataVolume.reuseAcrossReservations -and
         $validated.DataVolume.notBootable)

    $qmpPort = Get-QacS22R10QmpPortCandidate
    $qemuArgs = New-QacS22R10QemuArguments -ValidatedManifest $validated `
        -QemuPath 'C:\Program Files\qemu\qemu-system-x86_64.exe' -QmpPort $qmpPort
    $drives = @(); for ($i = 0; $i -lt $qemuArgs.Count; $i++) { if ($qemuArgs[$i] -eq '-drive') { $drives += $qemuArgs[$i + 1] } }
    $devices = @(); for ($i = 0; $i -lt $qemuArgs.Count; $i++) { if ($qemuArgs[$i] -eq '-device') { $devices += $qemuArgs[$i + 1] } }
    Assert-True 'launch has read-only firmware, read-only CD, and the single raw writable data image' `
        ($drives.Count -eq 3 -and $drives[0] -match 'readonly=on' -and $drives[1] -match 'readonly=on' -and
         $drives[2] -ceq "if=none,id=userdata,file=$dataPath,format=raw,cache=writeback")
    Assert-True 'data disk uses a stable ATA serial and CD is the only boot target' `
        ($devices.Count -eq 2 -and $devices -contains 'ide-hd,drive=userdata,serial=GXOS-DATA-V1' -and
         $devices -contains 'ide-cd,drive=releasecdrom,bootindex=1' -and $qemuArgs -contains 'order=d')
    Assert-True 'network, host filesystem sharing and HMP remain disabled' `
        ($qemuArgs -contains '-net' -and $qemuArgs -contains 'none' -and $qemuArgs -contains '-monitor' -and
         $qemuArgs -contains 'none' -and ($qemuArgs -join ' ') -notmatch '(?i)virtio-9p|virtiofs|vhost-user|smb=|hostfwd=|fat:rw:')

    $runnerText = [IO.File]::ReadAllText((Join-Path $repoRoot 'scripts\run-qac-s22r10-uefi.ps1'))
    $preLaunchIdentityIndex = $runnerText.IndexOf('persistent-data-image-prelaunch-identity-verified', [StringComparison]::Ordinal)
    $qacProcessStartIndex = $runnerText.IndexOf('$script:QacProcess = New-QacExecutionProcess', [StringComparison]::Ordinal)
    $qmpContinuationIndex = $runnerText.IndexOf("Invoke-QacS22R10Qmp -Client `$script:QmpClient -Execute 'cont'", [StringComparison]::Ordinal)
    $betweenQacLaunchAndContinuation = if ($qacProcessStartIndex -ge 0 -and $qmpContinuationIndex -gt $qacProcessStartIndex) {
        $runnerText.Substring($qacProcessStartIndex, $qmpContinuationIndex - $qacProcessStartIndex)
    } else { '' }
    Assert-True 'writable image identity is checked before QEMU opens it' `
        ($preLaunchIdentityIndex -ge 0 -and $preLaunchIdentityIndex -lt $qacProcessStartIndex -and
         $betweenQacLaunchAndContinuation -notmatch 'Get-QacS22R10ArtifactIdentity -Path \$DataImagePath')

    $tampered = $validated.Manifest
    $tampered.launchContract.arguments += @('-drive', 'if=ide,file=extra.img,format=raw')
    Write-TestJson $manifestPath $tampered
    $tamperedValidated = Get-QacS22R10ValidatedManifest -ManifestPath $manifestPath -PublicPreviewIsoOnly `
        -PersistentDataVolume -ExpectedDataImagePath $dataPath -ExpectedDataVolumeRoot $dataRoot `
        -ExpectedDataImageSha256 $dataHash
    Assert-Throws 'additional writable guest disks fail before launch' {
        New-QacS22R10QemuArguments -ValidatedManifest $tamperedValidated `
            -QemuPath 'qemu-system-x86_64.exe' -QmpPort $qmpPort
    } 'unexpected number of block drives|only the verified dedicated writable raw data image'

    $tampered.launchContract.arguments = @($originalArguments)
    $deviceArgumentIndex = [Array]::IndexOf([string[]]$tampered.launchContract.arguments, 'ide-hd,drive=userdata,serial=GXOS-DATA-V1')
    if ($deviceArgumentIndex -lt 0) { throw 'Fixture could not locate the expected stable ATA serial argument.' }
    $tampered.launchContract.arguments[$deviceArgumentIndex] = 'ide-hd,drive=userdata,serial=OTHER-DISK'
    Write-TestJson $manifestPath $tampered
    Assert-Throws 'wrong stable device identity fails QEMU launch construction' {
        $wrongIdentity = Get-QacS22R10ValidatedManifest -ManifestPath $manifestPath -PublicPreviewIsoOnly `
            -PersistentDataVolume -ExpectedDataImagePath $dataPath -ExpectedDataVolumeRoot $dataRoot `
            -ExpectedDataImageSha256 $dataHash
        New-QacS22R10QemuArguments -ValidatedManifest $wrongIdentity `
            -QemuPath 'qemu-system-x86_64.exe' -QmpPort $qmpPort
    } 'only the verified dedicated writable raw data image'

    Write-Host "S22R22 QAC profile tests passed: $script:checks assertions. Data image is a synthetic zero-filled configuration fixture, not persistence evidence."
} finally {
    if (Test-Path -LiteralPath $dataPath -PathType Leaf) {
        Remove-Item -LiteralPath $dataPath -Force
    }
    if (Test-Path -LiteralPath $fixtureRoot -PathType Container) {
        $resolvedFixtureRoot = [IO.Path]::GetFullPath($fixtureRoot)
        $resolvedTempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
        if (-not $resolvedFixtureRoot.StartsWith($resolvedTempRoot, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove fixture path outside the temp root: $resolvedFixtureRoot"
        }
        Remove-Item -LiteralPath $resolvedFixtureRoot -Recurse -Force
    }
}
