$ErrorActionPreference = 'Stop'

# S22R20 hosted protocol fixture. These Ed25519 keys and bytes are synthetic;
# this script does not create, read, or use a guideXOS production key.
$Namespace = 'guidexos-server-release-manifest-v1'
$Principal = 'guideXOS-test'
$ExpectedVersion = '0.3.0'
$ExpectedChannel = 'public-technical-preview'
$ExpectedArchitecture = 'amd64'
$ExpectedProfile = 'ServerV03PublicPreviewPacManOnly'
$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)

$sshCommand = Get-Command ssh-keygen.exe -ErrorAction SilentlyContinue
if (-not $sshCommand) {
    $sshCommand = Get-Command ssh-keygen -ErrorAction SilentlyContinue
}
if (-not $sshCommand) {
    throw 'OpenSSH ssh-keygen is required for this hosted fixture test.'
}
$SshKeygen = $sshCommand.Source

$tempRoot = Join-Path $env:TEMP ('s22r20-auth-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempRoot | Out-Null

function Invoke-SshKeygen {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string[]]$Arguments)

    $quotedArguments = foreach ($argument in $Arguments) {
        if ($argument.Length -gt 0 -and $argument -notmatch '[\s"]') {
            $argument
            continue
        }

        $builder = New-Object System.Text.StringBuilder
        [void]$builder.Append([char]34)
        $backslashCount = 0
        foreach ($character in $argument.ToCharArray()) {
            if ($character -eq [char]92) {
                $backslashCount++
                continue
            }
            if ($character -eq [char]34) {
                [void]$builder.Append(('\' * (($backslashCount * 2) + 1) -join ''))
                [void]$builder.Append([char]34)
                $backslashCount = 0
                continue
            }
            [void]$builder.Append(('\' * $backslashCount -join ''))
            [void]$builder.Append($character)
            $backslashCount = 0
        }
        [void]$builder.Append(('\' * ($backslashCount * 2) -join ''))
        [void]$builder.Append([char]34)
        $builder.ToString()
    }

    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $SshKeygen
    $startInfo.Arguments = $quotedArguments -join ' '
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $startInfo
    [void]$process.Start()
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        throw "ssh-keygen failed ($($process.ExitCode)): $stdout $stderr"
    }
}

function New-SyntheticKey {
    param([Parameter(Mandatory = $true)][string]$Path)

    Invoke-SshKeygen @('-q', '-t', 'ed25519', '-N', '', '-C', 'S22R20 synthetic fixture only', '-f', $Path)
}

function Write-AllowedSigners {
    param(
        [Parameter(Mandatory = $true)][string]$PublicKeyPath,
        [Parameter(Mandatory = $true)][string]$Path
    )

    $publicKey = (Get-Content -LiteralPath $PublicKeyPath -Raw).Trim()
    $line = "$Principal namespaces=`"$Namespace`" $publicKey`n"
    [IO.File]::WriteAllText($Path, $line, [Text.Encoding]::ASCII)
}

function Write-ManifestBytes {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Version,
        [Parameter(Mandatory = $true)][string]$Digest,
        [Parameter(Mandatory = $true)][long]$Size,
        [string]$RawText
    )

    if (-not $PSBoundParameters.ContainsKey('RawText')) {
        # Fixed key ordering and compact UTF-8 bytes model the proposed
        # canonical manifest fixture. The production format still needs a
        # strict RFC 8785/JCS validator and duplicate-key rejection.
        $RawText = '{"architecture":"' + $ExpectedArchitecture + '","channel":"' + $ExpectedChannel + '","format":"guideXOS-release-manifest","formatVersion":1,"iso":{"fileName":"candidate.iso","sha256":"' + $Digest + '","size":' + $Size.ToString([Globalization.CultureInfo]::InvariantCulture) + '},"product":"guideXOS Server","releaseProfile":"' + $ExpectedProfile + '","version":"' + $Version + '"}'
    }
    [IO.File]::WriteAllBytes($Path, $Utf8NoBom.GetBytes($RawText))
}

function Sign-Manifest {
    param(
        [Parameter(Mandatory = $true)][string]$PrivateKeyPath,
        [Parameter(Mandatory = $true)][string]$ManifestPath
    )

    Invoke-SshKeygen @('-Y', 'sign', '-f', $PrivateKeyPath, '-n', $Namespace, $ManifestPath)
}

function Test-Signature {
    param(
        [Parameter(Mandatory = $true)][string]$ManifestPath,
        [Parameter(Mandatory = $true)][string]$SignaturePath,
        [Parameter(Mandatory = $true)][string]$AllowedSignersPath
    )

    if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf) -or
        -not (Test-Path -LiteralPath $SignaturePath -PathType Leaf) -or
        -not (Test-Path -LiteralPath $AllowedSignersPath -PathType Leaf)) {
        return $false
    }

    $command = 'type "' + $ManifestPath + '" | "' + $SshKeygen + '" -Y verify -f "' + $AllowedSignersPath + '" -I ' + $Principal + ' -n ' + $Namespace + ' -s "' + $SignaturePath + '"'
    $previousErrorAction = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = & $env:ComSpec /d /c $command 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousErrorAction
    }
    if ($exitCode -ne 0) {
        Write-Host "  ssh-keygen verify rejected: $($output -join ' ')"
    }
    return ($exitCode -eq 0)
}

function Test-ManifestAndArtifact {
    param(
        [Parameter(Mandatory = $true)][string]$ManifestPath,
        [Parameter(Mandatory = $true)][string]$ArtifactPath
    )

    try {
        $raw = [IO.File]::ReadAllBytes($ManifestPath)
        $text = $Utf8NoBom.GetString($raw)
        $document = $text | ConvertFrom-Json -ErrorAction Stop
        if ($document.format -cne 'guideXOS-release-manifest' -or
            $document.formatVersion -ne 1 -or
            $document.product -cne 'guideXOS Server' -or
            $document.version -cne $ExpectedVersion -or
            $document.channel -cne $ExpectedChannel -or
            $document.architecture -cne $ExpectedArchitecture -or
            $document.releaseProfile -cne $ExpectedProfile) {
            return $false
        }
        if ($document.iso.fileName -cne 'candidate.iso' -or
            $document.iso.sha256 -cnotmatch '^[0-9a-f]{64}$' -or
            -not ($document.iso.size -is [int] -or $document.iso.size -is [long]) -or
            [long]$document.iso.size -le 0) {
            return $false
        }
        $canonicalText = '{"architecture":"' + $document.architecture + '","channel":"' + $document.channel + '","format":"' + $document.format + '","formatVersion":' + ([int]$document.formatVersion).ToString([Globalization.CultureInfo]::InvariantCulture) + ',"iso":{"fileName":"' + $document.iso.fileName + '","sha256":"' + $document.iso.sha256 + '","size":' + ([long]$document.iso.size).ToString([Globalization.CultureInfo]::InvariantCulture) + '},"product":"' + $document.product + '","releaseProfile":"' + $document.releaseProfile + '","version":"' + $document.version + '"}'
        if ($text -cne $canonicalText) {
            return $false
        }
        if (-not (Test-Path -LiteralPath $ArtifactPath -PathType Leaf)) {
            return $false
        }
        $artifact = Get-Item -LiteralPath $ArtifactPath
        $digest = (Get-FileHash -LiteralPath $ArtifactPath -Algorithm SHA256).Hash.ToLowerInvariant()
        return ($artifact.Length -eq [long]$document.iso.size -and $digest -ceq $document.iso.sha256)
    }
    catch {
        return $false
    }
}

function Test-FullVerification {
    param(
        [Parameter(Mandatory = $true)][string]$ManifestPath,
        [Parameter(Mandatory = $true)][string]$SignaturePath,
        [Parameter(Mandatory = $true)][string]$AllowedSignersPath,
        [Parameter(Mandatory = $true)][string]$ArtifactPath
    )

    if (-not (Test-Signature -ManifestPath $ManifestPath -SignaturePath $SignaturePath -AllowedSignersPath $AllowedSignersPath)) {
        return $false
    }
    return (Test-ManifestAndArtifact -ManifestPath $ManifestPath -ArtifactPath $ArtifactPath)
}

function Assert-ExpectedResult {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][bool]$Actual,
        [Parameter(Mandatory = $true)][bool]$Expected
    )

    if ($Actual -ne $Expected) {
        throw "FAIL $Name (expected accepted=$Expected, got accepted=$Actual)"
    }
    Write-Host "PASS $Name"
}

try {
    $privateKey = Join-Path $tempRoot 'synthetic-release-key'
    $wrongPrivateKey = Join-Path $tempRoot 'wrong-synthetic-key'
    $allowedSigners = Join-Path $tempRoot 'allowed_signers'
    $wrongAllowedSigners = Join-Path $tempRoot 'wrong_allowed_signers'
    $artifactPath = Join-Path $tempRoot 'candidate.iso'
    $manifestPath = Join-Path $tempRoot 'release.manifest.json'

    New-SyntheticKey -Path $privateKey
    New-SyntheticKey -Path $wrongPrivateKey
    Write-AllowedSigners -PublicKeyPath ($privateKey + '.pub') -Path $allowedSigners
    Write-AllowedSigners -PublicKeyPath ($wrongPrivateKey + '.pub') -Path $wrongAllowedSigners

    [IO.File]::WriteAllBytes($artifactPath, $Utf8NoBom.GetBytes("S22R20 synthetic artifact fixture`n"))
    $artifactInfo = Get-Item -LiteralPath $artifactPath
    $artifactDigest = (Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-ManifestBytes -Path $manifestPath -Version $ExpectedVersion -Digest $artifactDigest -Size $artifactInfo.Length
    Sign-Manifest -PrivateKeyPath $privateKey -ManifestPath $manifestPath
    $signaturePath = $manifestPath + '.sig'
    Assert-ExpectedResult 'synthetic key signs exact manifest; signed identity and artifact digest verify' (Test-FullVerification $manifestPath $signaturePath $allowedSigners $artifactPath) $true

    $changedArtifact = Join-Path $tempRoot 'changed.iso'
    [IO.File]::WriteAllBytes($changedArtifact, $Utf8NoBom.GetBytes("modified synthetic artifact`n"))
    Assert-ExpectedResult 'modified artifact is rejected by signed size/hash' (Test-FullVerification $manifestPath $signaturePath $allowedSigners $changedArtifact) $false

    $changedManifest = Join-Path $tempRoot 'changed.manifest.json'
    $changedText = [IO.File]::ReadAllText($manifestPath, $Utf8NoBom).Replace('"version":"0.3.0"', '"version":"9.9.9"')
    [IO.File]::WriteAllText($changedManifest, $changedText, $Utf8NoBom)
    Copy-Item -LiteralPath $signaturePath -Destination ($changedManifest + '.sig')
    Assert-ExpectedResult 'modified manifest is rejected by signature verification' (Test-FullVerification $changedManifest ($changedManifest + '.sig') $allowedSigners $artifactPath) $false

    Assert-ExpectedResult 'incorrect public key is rejected' (Test-FullVerification $manifestPath $signaturePath $wrongAllowedSigners $artifactPath) $false

    $missingSignaturePath = Join-Path $tempRoot 'missing.sig'
    Assert-ExpectedResult 'missing signature is rejected' (Test-FullVerification $manifestPath $missingSignaturePath $allowedSigners $artifactPath) $false

    $unsupportedSignaturePath = Join-Path $tempRoot 'unsupported.sig'
    [IO.File]::WriteAllText($unsupportedSignaturePath, 'unsupported signature fixture', [Text.Encoding]::ASCII)
    Assert-ExpectedResult 'unsupported signature format is rejected' (Test-FullVerification $manifestPath $unsupportedSignaturePath $allowedSigners $artifactPath) $false

    $wrongIdentity = Join-Path $tempRoot 'wrong-identity.manifest.json'
    Write-ManifestBytes -Path $wrongIdentity -Version '9.9.9' -Digest $artifactDigest -Size $artifactInfo.Length
    Sign-Manifest -PrivateKeyPath $privateKey -ManifestPath $wrongIdentity
    Assert-ExpectedResult 'valid signature with incorrect release identity is rejected' (Test-FullVerification $wrongIdentity ($wrongIdentity + '.sig') $allowedSigners $artifactPath) $false

    $invalidHash = Join-Path $tempRoot 'invalid-hash.manifest.json'
    Write-ManifestBytes -Path $invalidHash -Version $ExpectedVersion -Digest 'not-a-sha256' -Size $artifactInfo.Length
    Sign-Manifest -PrivateKeyPath $privateKey -ManifestPath $invalidHash
    Assert-ExpectedResult 'valid signature with invalid hash metadata is rejected' (Test-FullVerification $invalidHash ($invalidHash + '.sig') $allowedSigners $artifactPath) $false

    $duplicateKey = Join-Path $tempRoot 'duplicate-key.manifest.json'
    $canonicalFixture = [IO.File]::ReadAllText($manifestPath, $Utf8NoBom)
    $closingBrace = $canonicalFixture.LastIndexOf('}')
    $duplicateText = $canonicalFixture.Substring(0, $closingBrace) + ',"version":"0.3.0"}'
    Write-ManifestBytes -Path $duplicateKey -Version $ExpectedVersion -Digest $artifactDigest -Size $artifactInfo.Length -RawText $duplicateText
    Sign-Manifest -PrivateKeyPath $privateKey -ManifestPath $duplicateKey
    Assert-ExpectedResult 'valid signature over duplicate-key JSON is rejected as noncanonical' (Test-FullVerification $duplicateKey ($duplicateKey + '.sig') $allowedSigners $artifactPath) $false

    $truncated = Join-Path $tempRoot 'truncated.manifest.json'
    Write-ManifestBytes -Path $truncated -Version $ExpectedVersion -Digest $artifactDigest -Size $artifactInfo.Length -RawText '{"architecture":"amd64"'
    Sign-Manifest -PrivateKeyPath $privateKey -ManifestPath $truncated
    Assert-ExpectedResult 'valid signature over truncated/malformed metadata is rejected' (Test-FullVerification $truncated ($truncated + '.sig') $allowedSigners $artifactPath) $false

    Write-Host 'S22R20 synthetic OpenSSH SSHSIG fixture: 10 total cases passed (1 accepted, 9 rejected); no production key or artifact was used.'
}
finally {
    $resolvedTemp = [IO.Path]::GetFullPath($tempRoot)
    $resolvedParent = [IO.Path]::GetFullPath($env:TEMP).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $resolvedTemp.StartsWith($resolvedParent, [StringComparison]::OrdinalIgnoreCase) -or
        -not ([IO.Path]::GetFileName($resolvedTemp).StartsWith('s22r20-auth-', [StringComparison]::Ordinal))) {
        throw "Refusing to remove unexpected temporary path: $resolvedTemp"
    }
    [IO.Directory]::Delete($resolvedTemp, $true)
}
