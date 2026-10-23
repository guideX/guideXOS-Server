[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ElfPath,
    [UInt64]$RuntimeImageBase = 0x60C000
)

$ErrorActionPreference = 'Stop'
$ReadElf = 'C:\mingw64\bin\readelf.exe'
$Objdump = 'C:\mingw64\bin\objdump.exe'
foreach ($tool in @($ReadElf, $Objdump)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
        throw "Required Native ELF relocation audit tool is missing: $tool"
    }
}

$ElfPath = [IO.Path]::GetFullPath($ElfPath)
if (-not (Test-Path -LiteralPath $ElfPath -PathType Leaf)) {
    throw "Native ELF candidate is missing: $ElfPath"
}

$header = (& $ReadElf -h $ElfPath 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0 -or $header -notmatch 'Class:\s+ELF64' -or
    $header -notmatch 'Type:\s+EXEC' -or
    $header -notmatch 'Machine:\s+Advanced Micro Devices X86-64') {
    throw 'Relocation regression requires a static AMD64 ELF64 ET_EXEC candidate.'
}

$programHeaders = (& $ReadElf -lW $ElfPath 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0) { throw 'readelf could not inspect the Native ELF program headers.' }
$loads = @(
    foreach ($line in ($programHeaders -split "`r?`n")) {
        if ($line -match '^\s*LOAD\s+0x(?<offset>[0-9a-fA-F]+)\s+0x(?<vaddr>[0-9a-fA-F]+)\s+0x[0-9a-fA-F]+\s+0x(?<filesz>[0-9a-fA-F]+)\s+0x(?<memsz>[0-9a-fA-F]+)\s+.*\s+0x(?<align>[0-9a-fA-F]+)\s*$') {
            [pscustomobject]@{
                Offset = [Convert]::ToUInt64($Matches.offset, 16)
                Vaddr = [Convert]::ToUInt64($Matches.vaddr, 16)
                FileSize = [Convert]::ToUInt64($Matches.filesz, 16)
                MemSize = [Convert]::ToUInt64($Matches.memsz, 16)
                Alignment = [Convert]::ToUInt64($Matches.align, 16)
            }
        }
    }
)
if ($loads.Count -eq 0) { throw 'Native ELF has no readable PT_LOAD geometry.' }

$minimum = [UInt64]::MaxValue
$maximum = [UInt64]0
$maxAlignment = [UInt64]1
foreach ($load in $loads) {
    if ($load.Vaddr -lt $minimum) { $minimum = $load.Vaddr }
    $end = [UInt64]($load.Vaddr + $load.MemSize)
    if ($end -gt $maximum) { $maximum = $end }
    if ($load.Alignment -gt $maxAlignment) { $maxAlignment = $load.Alignment }
}
if ($RuntimeImageBase -lt $minimum -or
    ($RuntimeImageBase % $maxAlignment) -ne ($minimum % $maxAlignment)) {
    throw ('Runtime image base 0x{0:X} does not preserve the ELF PT_LOAD alignment residue.' -f $RuntimeImageBase)
}
$delta = [Int64]($RuntimeImageBase - $minimum)

$fileBytes = [IO.File]::ReadAllBytes($ElfPath)
function Find-StringAddresses([string]$text) {
    $textBytes = [Text.Encoding]::ASCII.GetBytes($text)
    $needle = New-Object byte[] ($textBytes.Length + 1)
    [Array]::Copy($textBytes, $needle, $textBytes.Length)
    $found = New-Object 'System.Collections.Generic.List[UInt64]'
    for ($index = 0; $index -le $fileBytes.Length - $needle.Length; $index++) {
        $matches = $true
        for ($byteIndex = 0; $byteIndex -lt $needle.Length; $byteIndex++) {
            if ($fileBytes[$index + $byteIndex] -ne $needle[$byteIndex]) {
                $matches = $false
                break
            }
        }
        if (-not $matches) { continue }
        if ($text -ceq 'available' -and $index -gt 0 -and $fileBytes[$index - 1] -eq 0x6E) {
            continue
        }
        foreach ($load in $loads) {
            if ([UInt64]$index -ge $load.Offset -and
                [UInt64]($index + $needle.Length) -le ($load.Offset + $load.FileSize)) {
                $found.Add([UInt64]($load.Vaddr + [UInt64]$index - $load.Offset))
            }
        }
    }
    return $found.ToArray()
}

$disassembly = (& $Objdump -d -Mintel -w $ElfPath 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0) { throw 'objdump could not disassemble the Native ELF executable segments.' }
$movabsTargets = New-Object 'System.Collections.Generic.HashSet[UInt64]'
foreach ($match in [regex]::Matches($disassembly, '(?im)\bmovabs\s+[a-z0-9]+\s*,\s*0x(?<address>[0-9a-f]+)')) {
    [void]$movabsTargets.Add([Convert]::ToUInt64($match.Groups['address'].Value, 16))
}

$requiredStrings = @(
    [pscustomobject]@{ Name = 'window title'; Text = 'Stellar Navigator SN10' }
    [pscustomobject]@{ Name = 'DE440s file path'; Text = 'Astronomy.Ephemeris/Data/ephemeris_de440s_chebyshev.sn4bin' }
    [pscustomobject]@{ Name = 'available result status'; Text = 'available' }
    [pscustomobject]@{ Name = 'unavailable result status'; Text = 'unavailable' }
)
$translations = New-Object 'System.Collections.Generic.List[string]'
foreach ($item in $requiredStrings) {
    $addresses = @(Find-StringAddresses $item.Text)
    if ($addresses.Count -eq 0) { throw "Expected NUL-terminated Native ELF string is missing: $($item.Name)" }
    $translated = $false
    foreach ($address in $addresses) {
        if (-not $movabsTargets.Contains([UInt64]$address)) { continue }
        if ($address -lt $minimum -or $address -ge $maximum) {
            throw "Static pointer target for $($item.Name) falls outside PT_LOAD memory."
        }
        $runtimeAddress = [UInt64]($address + $delta)
        $expectedRuntimeAddress = [UInt64]($RuntimeImageBase + ($address - $minimum))
        if ($runtimeAddress -ne $expectedRuntimeAddress -or
            $runtimeAddress -lt $RuntimeImageBase -or
            $runtimeAddress -ge ($RuntimeImageBase + ($maximum - $minimum))) {
            throw "Static pointer translation failed for $($item.Name)."
        }
        $translations.Add(('{0}=0x{1:X}->0x{2:X}' -f $item.Name, $address, $runtimeAddress))
        $translated = $true
        break
    }
    if (-not $translated) {
        throw "Static pointer for $($item.Name) is not encoded as a loader-rebasable 64-bit MOVABS immediate."
    }
}

$unsupportedAddressPatterns = @(
    '(?im)\bmov\s+(?:e(?:ax|bx|cx|dx|si|di|sp|bp)|r(?:[89]d|1[0-5]d))\s*,\s*0x(?<address>[0-9a-f]+)',
    '(?im)\bmov\s+QWORD PTR\s+\[[^\]]+\]\s*,\s*0x(?<address>[0-9a-f]+)',
    '(?im)\bpush\s+0x(?<address>[0-9a-f]+)'
)
foreach ($pattern in $unsupportedAddressPatterns) {
    foreach ($match in [regex]::Matches($disassembly, $pattern)) {
        $address = [Convert]::ToUInt64($match.Groups['address'].Value, 16)
        if ($address -ge $minimum -and $address -lt $maximum) {
            throw ('Unsupported link-time address immediate remains in the image: {0}' -f $match.Value.Trim())
        }
    }
}

$relocations = (& $ReadElf -r $ElfPath 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0 -or $relocations -notmatch 'There are no relocations') {
    throw 'Native ELF relocation contract changed; this package expects a static ET_EXEC with no runtime relocation table.'
}

Write-Host ('[SN10] native ELF relocation contract PASS: minVaddr=0x{0:X} maxVaddr=0x{1:X} runtimeBase=0x{2:X}; {3}' -f `
    $minimum, $maximum, $RuntimeImageBase, ($translations -join ', '))
