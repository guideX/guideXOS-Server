[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PreviousArchive,
    [Parameter(Mandatory = $true)]
    [string]$FreshArchive,
    [Parameter(Mandatory = $true)]
    [string]$FreshRepeatArchive,
    [Parameter(Mandatory = $true)]
    [string]$OutputRoot,
    [string]$RepoRoot = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-Hash([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Assert-Within([string]$Path, [string]$Root, [string]$Label) {
    $full = [IO.Path]::GetFullPath($Path)
    $base = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $full.StartsWith($base, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label escapes its evidence root: $full"
    }
}

function Find-Tool([string]$Name) {
    foreach ($candidate in @(
        "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64\$Name",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\HostX64\x64\$Name"
    )) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    }
    throw "MSVC tool not found: $Name"
}

function Read-Ascii([byte[]]$Bytes, [int]$Offset, [int]$Length) {
    [Text.Encoding]::ASCII.GetString($Bytes, $Offset, $Length)
}

function Resolve-ArchiveName([string]$RawName, [byte[]]$LongNames) {
    $name = $RawName.Trim()
    if ($name -match '^/(\d+)$') {
        $offset = [int]$matches[1]
        if ($offset -ge $LongNames.Length) { throw "Archive long-name offset is out of range: $offset" }
        $end = $offset
        while ($end -lt $LongNames.Length -and $LongNames[$end] -ne 0 -and $LongNames[$end] -ne 10) { $end++ }
        return [Text.Encoding]::ASCII.GetString($LongNames, $offset, $end - $offset).TrimEnd('/')
    }
    return $name.TrimEnd('/')
}

function Read-Archive([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    $signature = [Text.Encoding]::ASCII.GetBytes("!<arch>`n")
    if ($bytes.Length -lt $signature.Length) { throw "Not a COFF archive: $Path" }
    for ($i = 0; $i -lt $signature.Length; $i++) {
        if ($bytes[$i] -ne $signature[$i]) { throw "Not a COFF archive: $Path" }
    }

    $rawMembers = [Collections.Generic.List[object]]::new()
    $offset = $signature.Length
    while ($offset -lt $bytes.Length) {
        if ($offset + 60 -gt $bytes.Length) { throw "Truncated archive header: $Path" }
        $sizeText = (Read-Ascii $bytes ($offset + 48) 10).Trim()
        $size = 0
        if (-not [int]::TryParse($sizeText, [Globalization.NumberStyles]::Integer,
                [Globalization.CultureInfo]::InvariantCulture, [ref]$size)) {
            throw "Invalid archive member size at offset ${offset}: $Path"
        }
        $dataStart = $offset + 60
        $dataEnd = $dataStart + $size
        if ($dataEnd -gt $bytes.Length) { throw "Archive member exceeds file: $Path" }
        $rawMembers.Add([pscustomobject]@{
            offset = $offset
            rawName = (Read-Ascii $bytes $offset 16).Trim()
            date = (Read-Ascii $bytes ($offset + 16) 12).Trim()
            uid = (Read-Ascii $bytes ($offset + 28) 6).Trim()
            gid = (Read-Ascii $bytes ($offset + 34) 6).Trim()
            mode = (Read-Ascii $bytes ($offset + 40) 8).Trim()
            size = $size
            content = $bytes[$dataStart..($dataEnd - 1)]
        })
        $offset = $dataEnd
        if (($offset % 2) -ne 0) { $offset++ }
    }

    $longNameMember = $rawMembers | Where-Object { $_.rawName.Trim() -eq '//' } | Select-Object -First 1
    $longNames = if ($null -eq $longNameMember) { [byte[]]@() } else { [byte[]]$longNameMember.content }
    $members = [Collections.Generic.List[object]]::new()
    foreach ($member in $rawMembers) {
        $name = Resolve-ArchiveName $member.rawName $longNames
        $members.Add([pscustomobject]@{
            index = $members.Count
            offset = $member.offset
            rawName = $member.rawName
            name = $name
            content = [byte[]]$member.content
            size = $member.size
            metadata = [ordered]@{
                date = $member.date
                uid = $member.uid
                gid = $member.gid
                mode = $member.mode
            }
        })
    }
    return $members.ToArray()
}

function Get-NormalizedMemberName([string]$Name) {
    $normalized = $Name.Replace('/', '\')
    if ($normalized -match '(?i)\\(guidexos_[^\\]+\.obj)$') { return $matches[1].ToLowerInvariant() }
    return $normalized.ToLowerInvariant()
}

function Get-NormalizedObjectBytes([byte[]]$Bytes) {
    if ($Bytes.Length -lt 4) { return $Bytes }
    $sectionCount = [BitConverter]::ToUInt16($Bytes, 2)
    $sections = [Collections.Generic.List[object]]::new()
    for ($section = 0; $section -lt $sectionCount; $section++) {
        $header = 20 + (40 * $section)
        if ($header + 40 -gt $Bytes.Length) { break }
        $name = ([Text.Encoding]::ASCII.GetString($Bytes, $header, 8)).Trim([char]0)
        # .debug$S contains the compiler output path and .chks64 is a compiler
        # checksum table.  Neither is executable/member semantic content.
        if ($name -eq '.debug$S' -or $name -eq '.chks64') { continue }
        $rawLength = [BitConverter]::ToUInt32($Bytes, $header + 16)
        $rawPointer = [BitConverter]::ToUInt32($Bytes, $header + 20)
        $relocPointer = [BitConverter]::ToUInt32($Bytes, $header + 24)
        $relocCount = [BitConverter]::ToUInt16($Bytes, $header + 32)
        $rawHash = ''
        if ($rawLength -gt 0 -and $rawPointer -gt 0 -and [int]$rawPointer + [int]$rawLength -le $Bytes.Length) {
            $raw = $Bytes[[int]$rawPointer..([int]$rawPointer + [int]$rawLength - 1)]
            $digest = [Security.Cryptography.SHA256]::Create().ComputeHash([byte[]]$raw)
            $rawHash = [BitConverter]::ToString($digest) -replace '-', ''
        }
        $relocations = [Collections.Generic.List[string]]::new()
        for ($reloc = 0; $reloc -lt $relocCount; $reloc++) {
            $entry = [int]$relocPointer + (10 * $reloc)
            if ($entry + 10 -gt $Bytes.Length) { break }
            $virtualAddress = [BitConverter]::ToUInt32($Bytes, $entry)
            $type = [BitConverter]::ToUInt16($Bytes, $entry + 8)
            $relocations.Add(('{0:X8}:{1:X4}' -f $virtualAddress, $type))
        }
        $sections.Add([ordered]@{
            index = $section
            name = $name
            virtualSize = [BitConverter]::ToUInt32($Bytes, $header + 8)
            virtualAddress = [BitConverter]::ToUInt32($Bytes, $header + 12)
            rawSize = $rawLength
            rawSha256 = $rawHash
            characteristics = [BitConverter]::ToUInt32($Bytes, $header + 36)
            relocations = @($relocations)
        })
    }
    $projection = [ordered]@{
        machine = [BitConverter]::ToUInt16($Bytes, 0)
        sections = @($sections)
    }
    return [Text.Encoding]::UTF8.GetBytes(($projection | ConvertTo-Json -Compress -Depth 10))
}

function Get-SymbolInventory([string]$Path, [string]$Dumpbin) {
    $text = @(& $Dumpbin /nologo /symbols $Path 2>&1 | ForEach-Object { $_.ToString() })
    $defined = [Collections.Generic.List[string]]::new()
    $undefined = [Collections.Generic.List[string]]::new()
    foreach ($line in $text) {
        if ($line -match '^\s*[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+SECT\S*\s+.*\|\s+(.+)$') {
            $defined.Add((($matches[1] -split '\s+\(')[0]).Trim())
        } elseif ($line -match '^\s*[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+UNDEF\S*\s+.*\|\s+(.+)$') {
            $undefined.Add($matches[1].Trim())
        }
    }
    [ordered]@{
        defined = @($defined | Sort-Object -Unique)
        undefined = @($undefined | Sort-Object -Unique)
    }
}

function Get-ArchiveManifest([string]$Path, [string]$Dumpbin, [string]$Scratch) {
    $archive = Read-Archive $Path
    $manifest = [Collections.Generic.List[object]]::new()
    foreach ($member in $archive) {
        $rawHash = [Security.Cryptography.SHA256]::Create().ComputeHash([byte[]]$member.content)
        $normalizedContent = if ($member.name -match '(?i)\.obj$') {
            Get-NormalizedObjectBytes ([byte[]]$member.content)
        } else { [byte[]]$member.content }
        $normalizedHash = [Security.Cryptography.SHA256]::Create().ComputeHash([byte[]]$normalizedContent)
        $defined = @()
        $undefined = @()
        if ($member.name -match '(?i)\.obj$') {
            $objectPath = Join-Path $Scratch (('m{0:D3}.obj' -f $member.index))
            [IO.File]::WriteAllBytes($objectPath, [byte[]]$member.content)
            $symbols = Get-SymbolInventory $objectPath $Dumpbin
            $defined = @($symbols.defined)
            $undefined = @($symbols.undefined)
        }
        $manifest.Add([ordered]@{
            index = $member.index
            name = $member.name
            normalizedName = Get-NormalizedMemberName $member.name
            rawName = $member.rawName
            archiveOffset = $member.offset
            size = $member.size
            metadata = $member.metadata
            sha256 = ([BitConverter]::ToString($rawHash) -replace '-', '')
            normalizedContentSha256 = ([BitConverter]::ToString($normalizedHash) -replace '-', '')
            definedSymbols = $defined
            undefinedSymbols = $undefined
        })
    }
    return $manifest.ToArray()
}

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (Resolve-Path (Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) '..\..\..')).Path
}
$repo = [IO.Path]::GetFullPath($RepoRoot)
$output = [IO.Path]::GetFullPath($OutputRoot)
Assert-Within $output (Join-Path $repo 'out\dotnet') 'Identity evidence'
New-Item -ItemType Directory -Force -Path $output | Out-Null

$previous = [IO.Path]::GetFullPath($PreviousArchive)
$fresh = [IO.Path]::GetFullPath($FreshArchive)
$freshRepeat = [IO.Path]::GetFullPath($FreshRepeatArchive)
foreach ($path in @($previous, $fresh, $freshRepeat)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Archive not found: $path" }
}

$dumpbin = Find-Tool 'dumpbin.exe'
$scratchRoot = Join-Path $output 'scratch'
New-Item -ItemType Directory -Force -Path $scratchRoot | Out-Null
$archivePaths = [ordered]@{ previous = $previous; fresh = $fresh; freshRepeat = $freshRepeat }
$manifests = [ordered]@{}
foreach ($entry in $archivePaths.GetEnumerator()) {
    $scratch = Join-Path $scratchRoot $entry.Key
    New-Item -ItemType Directory -Force -Path $scratch | Out-Null
    $manifests[$entry.Key] = @(Get-ArchiveManifest $entry.Value $dumpbin $scratch)
    $manifests[$entry.Key] | ConvertTo-Json -Depth 12 | Set-Content (Join-Path $output "$($entry.Key)-archive-manifest.json") -Encoding UTF8
}

function Get-Comparison([object[]]$Left, [object[]]$Right) {
    $l = @{}; foreach ($item in $Left) { $l[$item.normalizedName] = $item }
    $r = @{}; foreach ($item in $Right) { $r[$item.normalizedName] = $item }
    # The two linker-index members are packaging metadata.  Their raw bytes
    # contain archive offsets and symbol-table indexes, so they are reported
    # through the archive hashes but excluded from member-content identity.
    $names = @($l.Keys + $r.Keys | Where-Object { $_ -ne '' } | Sort-Object -Unique)
    $rows = foreach ($name in $names) {
        $a = $l[$name]; $b = $r[$name]
        [pscustomobject][ordered]@{
            member = $name
            presentLeft = ($null -ne $a)
            presentRight = ($null -ne $b)
            orderLeft = if ($null -ne $a) { $a.index } else { $null }
            orderRight = if ($null -ne $b) { $b.index } else { $null }
            rawHashLeft = if ($null -ne $a) { $a.sha256 } else { $null }
            rawHashRight = if ($null -ne $b) { $b.sha256 } else { $null }
            normalizedHashLeft = if ($null -ne $a) { $a.normalizedContentSha256 } else { $null }
            normalizedHashRight = if ($null -ne $b) { $b.normalizedContentSha256 } else { $null }
            symbolInventoryEqual = if ($null -eq $a -or $null -eq $b) { $false } else {
                ((@($a.definedSymbols) -join "`n") -eq (@($b.definedSymbols) -join "`n")) -and
                ((@($a.undefinedSymbols) -join "`n") -eq (@($b.undefinedSymbols) -join "`n"))
            }
            rawEqual = ($null -ne $a -and $null -ne $b -and $a.sha256 -eq $b.sha256)
            normalizedEqual = ($null -ne $a -and $null -ne $b -and $a.normalizedContentSha256 -eq $b.normalizedContentSha256)
        }
    }
    return @($rows)
}

$freshCompare = Get-Comparison $manifests.fresh $manifests.freshRepeat
$historicalCompare = Get-Comparison $manifests.previous $manifests.fresh
$freshCompare | ConvertTo-Json -Depth 12 | Set-Content (Join-Path $output 'fresh-vs-repeat.json') -Encoding UTF8
$historicalCompare | ConvertTo-Json -Depth 12 | Set-Content (Join-Path $output 'previous-vs-fresh.json') -Encoding UTF8

$lock = Get-Content (Join-Path $repo 'tools\dotnet\runtime-pack\runtime-pack.lock.json') -Raw | ConvertFrom-Json
$sourceFiles = @(
    'tools\dotnet\runtime-pack\src\gcenv\guidexos_gcenv.cpp',
    'tools\dotnet\runtime-pack\src\gcenv\guidexos_gc_platform_services.cpp',
    'tools\dotnet\runtime-pack\src\gcenv\guidexos_gc_platform_services.h',
    'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_virtual_memory_adapter.cpp',
    'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_virtual_memory_adapter.h',
    'runtime\memory\guidexos_virtual_memory_region_baremetal.cpp',
    'runtime\memory\guidexos_virtual_memory_region.h',
    'runtime\synchronization\guidexos_event_baremetal.cpp',
    'runtime\synchronization\guidexos_event.h',
    'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_event_adapter.cpp',
    'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_event_adapter.h',
    'runtime\synchronization\guidexos_mutex_baremetal.cpp',
    'runtime\synchronization\guidexos_mutex.h',
    'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_critical_section_adapter.cpp',
    'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_critical_section_adapter.h',
    'runtime\thread\guidexos_native_thread_baremetal.cpp',
    'runtime\thread\guidexos_native_thread.h',
    'runtime\local_storage\guidexos_local_storage.cpp',
    'runtime\local_storage\guidexos_local_storage.h',
    'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_fls_adapter.cpp',
    'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_fls_adapter.h',
    'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_thread_adapter.cpp',
    'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_thread_adapter.h'
)
$sourceInventory = foreach ($relative in $sourceFiles) {
    $path = Join-Path $repo $relative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Locked source input missing: $relative" }
    [ordered]@{ path = $relative; sha256 = Get-Hash $path; length = (Get-Item $path).Length }
}

$envNames = @('NUGET_PACKAGES', 'VCToolsInstallDir', 'VCINSTALLDIR', 'WindowsSdkDir', 'WindowsSDKVersion', 'VSCMD_ARG_TGT_ARCH', 'PROCESSOR_ARCHITECTURE')
$environment = [ordered]@{}
foreach ($name in $envNames) { $environment[$name] = [Environment]::GetEnvironmentVariable($name) }
$environment['PATH_SHA256'] = if ($null -eq $env:Path) { $null } else {
    $bytes = [Text.Encoding]::UTF8.GetBytes($env:Path)
    $hash = [Security.Cryptography.SHA256]::Create().ComputeHash($bytes)
    [BitConverter]::ToString($hash) -replace '-', ''
}

$stockArchive = Join-Path $env:USERPROFILE '.nuget\packages\runtime.win-x64.microsoft.dotnet.ilcompiler\9.0.0\sdk\Runtime.WorkstationGC.lib'
$freshStable = (@($freshCompare | Where-Object { -not $_.normalizedEqual }).Count -eq 0) -and
               (@($freshCompare | Where-Object { $_.orderLeft -ne $_.orderRight }).Count -eq 0) -and
               (@($freshCompare | Where-Object { -not $_.symbolInventoryEqual }).Count -eq 0)
$historicalChangedMembers = @($historicalCompare | Where-Object { -not $_.normalizedEqual } |
    Select-Object -ExpandProperty member)
$historicalIdentity = if ($historicalChangedMembers.Count -eq 0) {
    'Identity A candidate: normalized member content/order/symbols stable; raw packaging metadata differs'
} elseif ($historicalChangedMembers.Count -eq 1 -and
          $historicalChangedMembers[0] -eq 'guidexos_virtual_memory_region.obj' -and
          $freshStable) {
    'Identity B: historical artifact differs only in the reviewed QEMU virtual-memory range expansion; fresh normalized identity is stable'
} else {
    'Identity C/D candidate: unexplained historical semantic difference or collector identity uncertainty'
}
$report = [ordered]@{
    schemaVersion = 1
    generatedUtc = [DateTime]::UtcNow.ToString('o')
    classification = [ordered]@{
        previousVsFresh = $historicalIdentity
        freshVsRepeat = if ($freshStable) {
            'Identity A candidate: normalized member content/order/symbols stable; raw packaging metadata differs'
        } else { 'Unexpected fresh-build semantic difference' }
    }
    lockedIdentity = [ordered]@{
        sourceCommit = $lock.ilCompiler.commit
        runtimePackVersion = $lock.ilCompiler.version
        architecture = $lock.architecture
        originalTarget = $lock.runtimeIdentifier
        gcInterface = $lock.adaptedWorkstationGc.gcInterface
        eeInterface = $lock.adaptedWorkstationGc.eeInterface
        stockArchiveSha256 = $lock.runtimePack.files.'sdk/Runtime.WorkstationGC.lib'.sha256.ToUpperInvariant()
        stockArchiveObservedSha256 = Get-Hash $stockArchive
        activePalArchiveSha256 = 'C617D95647A20862947B52A1301DF96FE9104E5A13F11BB0C016B8370DDE115F'
    }
    archives = [ordered]@{
        previous = [ordered]@{ path = $previous; sha256 = Get-Hash $previous; length = (Get-Item $previous).Length; members = @($manifests.previous).Count }
        fresh = [ordered]@{ path = $fresh; sha256 = Get-Hash $fresh; length = (Get-Item $fresh).Length; members = @($manifests.fresh).Count }
        freshRepeat = [ordered]@{ path = $freshRepeat; sha256 = Get-Hash $freshRepeat; length = (Get-Item $freshRepeat).Length; members = @($manifests.freshRepeat).Count }
    }
    comparisons = [ordered]@{
        freshVsRepeat = [ordered]@{
            rawDifferentMembers = @($freshCompare | Where-Object { -not $_.rawEqual }).Count
            normalizedDifferentMembers = @($freshCompare | Where-Object { -not $_.normalizedEqual }).Count
            orderDifferentMembers = @($freshCompare | Where-Object { $_.orderLeft -ne $_.orderRight }).Count
            symbolDifferentMembers = @($freshCompare | Where-Object { -not $_.symbolInventoryEqual }).Count
        }
        previousVsFresh = [ordered]@{
            rawDifferentMembers = @($historicalCompare | Where-Object { -not $_.rawEqual }).Count
            normalizedDifferentMembers = @($historicalCompare | Where-Object { -not $_.normalizedEqual }).Count
            orderDifferentMembers = @($historicalCompare | Where-Object { $_.orderLeft -ne $_.orderRight }).Count
            symbolDifferentMembers = @($historicalCompare | Where-Object { -not $_.symbolInventoryEqual }).Count
            changedMembers = $historicalChangedMembers
        }
    }
    replacementObjects = @($sourceInventory | Where-Object { $_.path -match '(?i)(gcenv|adapter|baremetal|local_storage)' })
    sourceInputs = $sourceInventory
    environment = $environment
    toolchain = [ordered]@{
        compiler = Find-Tool 'cl.exe'
        compilerVersion = (cmd.exe /d /c "`"$(Find-Tool 'cl.exe')`" 2>&1" | Select-Object -First 1).ToString()
        archiveManager = Find-Tool 'lib.exe'
        symbolTool = $dumpbin
    }
    removedMembers = @('nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\__\__\gc\windows\gcenv.windows.cpp.obj')
    addedMembers = @('guidexos_gcenv.obj','guidexos_mutex.obj','guidexos_event.obj','guidexos_nativeaot_critical_section_adapter.obj','guidexos_nativeaot_virtual_memory_adapter.obj','guidexos_nativeaot_event_adapter.obj','guidexos_nativeaot_fls_adapter.obj','guidexos_nativeaot_thread_adapter.obj','guidexos_native_thread.obj','guidexos_virtual_memory_region.obj','guidexos_local_storage.obj','guidexos_gc_platform_services.obj')
}
$report | ConvertTo-Json -Depth 16 | Set-Content (Join-Path $output 'normalized-comparison.json') -Encoding UTF8
Write-Output "Identity report: $(Join-Path $output 'normalized-comparison.json')"
Write-Output "Previous SHA-256: $($report.archives.previous.sha256)"
Write-Output "Fresh SHA-256: $($report.archives.fresh.sha256)"
Write-Output "Fresh repeat SHA-256: $($report.archives.freshRepeat.sha256)"
Write-Output "Fresh normalized differences: $($report.comparisons.freshVsRepeat.normalizedDifferentMembers)"
Write-Output "Historical normalized differences: $($report.comparisons.previousVsFresh.normalizedDifferentMembers)"
