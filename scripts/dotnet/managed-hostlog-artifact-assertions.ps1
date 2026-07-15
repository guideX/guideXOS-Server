Set-StrictMode -Version Latest

function Get-ManagedHostLogMapSymbolAddress([string]$Path, [string]$Symbol, [string]$Label) {
    $pattern = '^\s+[0-9A-Fa-f]{4}:[0-9A-Fa-f]{8}\s+' + [regex]::Escape($Symbol) + '\s+([0-9A-Fa-f]{16})\s+'
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match $pattern) {
            return [Convert]::ToUInt64($Matches[1], 16)
        }
    }

    throw "$Label symbol not found in map: $Symbol"
}

function Assert-ManagedHostLogReversePInvokeChain([string]$ElfPath, [string]$MapPath, [string]$PeDumpPath, [switch]$GuideXosRuntimePack, [switch]$ManagedAllocation, [switch]$RepeatedAllocation) {
    $managedMain = Get-ManagedHostLogMapSymbolAddress $MapPath "ManagedMain" "Managed entry"
    $reversePInvoke = Get-ManagedHostLogMapSymbolAddress $MapPath "RhpReversePInvoke" "RhpReversePInvoke"
    $reverseReturn = Get-ManagedHostLogMapSymbolAddress $MapPath "RhpReversePInvokeReturn" "RhpReversePInvokeReturn"

    $actual = [ordered]@{
        ManagedMain = $managedMain
        RhpReversePInvoke = $reversePInvoke
        RhpReversePInvokeReturn = $reverseReturn
    }

    $attach = [uint64]0
    $flsImport = [uint64]0
    if ($GuideXosRuntimePack) {
        Assert-ManagedHostLogFileContains $MapPath @('guidexos_nativeaot_platform\.obj', 'RhpReversePInvoke', 'RhpReversePInvokeReturn') "GuideXOS runtime-pack reverse-P/Invoke map evidence"
        Assert-ManagedHostLogFileNotContains $PeDumpPath @('FlsGetValue', 'FlsSetValue') "GuideXOS runtime-pack FLS import elimination"
    } else {
        $attach = Get-ManagedHostLogMapSymbolAddress $MapPath "RhpReversePInvokeAttachOrTrapThread2" "RhpReversePInvokeAttachOrTrapThread2"
        $flsImport = Get-ManagedHostLogMapSymbolAddress $MapPath "__imp_FlsGetValue" "FlsGetValue import thunk"
        $actual.RhpReversePInvokeAttachOrTrapThread2 = $attach
        $actual.FlsGetValueImportThunk = $flsImport
        $expected = [ordered]@{
            ManagedMain = [uint64]0x10001900
            RhpReversePInvoke = [uint64]0x1004B140
            RhpReversePInvokeAttachOrTrapThread2 = [uint64]0x1004B1A0
            RhpReversePInvokeReturn = [uint64]0x1004B290
            FlsGetValueImportThunk = [uint64]0x10052108
        }
        foreach ($name in $expected.Keys) {
            if ([uint64]$actual[$name] -ne [uint64]$expected[$name]) {
                throw "NativeAOT reverse-P/Invoke symbol drift for $name. Expected 0x$('{0:X}' -f $expected[$name]), got 0x$('{0:X}' -f $actual[$name])."
            }
        }
    }

    $elf = Read-ManagedHostLogElfEnvelope $ElfPath
    $executableSegment = $elf.LoadSegments | Where-Object { ($_.Flags -band 1) -ne 0 } | Select-Object -First 1
    if ($null -eq $executableSegment) { throw "No executable PT_LOAD found for reverse-P/Invoke chain assertion." }
    $entryFileOffset = [int]($executableSegment.Offset + ($elf.Entry - $executableSegment.VirtualAddress))
    $reverseCallFound = $false
    for ($offset = 0; $offset -lt 0x90; $offset++) {
        $callOffset = $entryFileOffset + $offset
        if ($elf.Bytes[$callOffset] -ne 0xE8) { continue }
        $relativeCall = [BitConverter]::ToInt32($elf.Bytes, $callOffset + 1)
        $callTarget = [uint64]($elf.Entry + $offset + 5 + [int64]$relativeCall)
        if ($callTarget -eq $reversePInvoke) {
            $reverseCallFound = $true
            break
        }
    }
    if (-not $reverseCallFound) {
        throw "ManagedMain no longer contains a direct call to RhpReversePInvoke in its entry prologue."
    }

    if ($ManagedAllocation) {
        Assert-ManagedHostLogFileContains $MapPath @(
            'RhpNewArray\s+[0-9A-Fa-f]{16}',
            'guideXosStockRhpNewArray',
            'g_guideXosManagedHeap',
            'g_guideXosAllocationDiagnostics'
        ) "Managed allocation runtime-pack evidence"
        if ($RepeatedAllocation) {
            Assert-ManagedHostLogFileContains $MapPath @(
                'guideXosManagedAllocationCanFit\s+[0-9A-Fa-f]{16}',
                'guideXosManagedAllocationValidateObject\s+[0-9A-Fa-f]{16}',
                'guideXosManagedAllocationRecordFailure\s+[0-9A-Fa-f]{16}',
                'guideXosManagedAllocationReport\s+[0-9A-Fa-f]{16}',
                '__pinvoke_HostLogProof__Module____Internal__guideXosManagedAllocationCanFit__Ansi',
                '__pinvoke_HostLogProof__Module____Internal__guideXosManagedAllocationValidateObject__Ansi',
                '__pinvoke_HostLogProof__Module____Internal__guideXosManagedAllocationRecordFailure__Ansi',
                '__pinvoke_HostLogProof__Module____Internal__guideXosManagedAllocationReport__Ansi'
            ) "Repeated allocation proof helper binding evidence"
        }
    }

    if (-not $GuideXosRuntimePack) {
        Assert-ManagedHostLogFileContains $PeDumpPath @(
            '00052108\s+<none>\s+[0-9A-Fa-f]+\s+FlsGetValue'
        ) "Reverse-P/Invoke Windows dependency evidence"
    }

    return [pscustomobject]@{
        EntryCategory = if ($GuideXosRuntimePack) { "guidexos-runtime-pack-reverse-pinvoke" } else { "runtime-correct-reverse-pinvoke-required" }
        ManagedMain = $managedMain
        RhpReversePInvoke = $reversePInvoke
        RhpReversePInvokeAttachOrTrapThread2 = $attach
        RhpReversePInvokeReturn = $reverseReturn
        FlsGetValueImportThunk = $flsImport
    }
}

function Get-ManagedHostLogImportTable([string]$Path) {
    $imports = [ordered]@{}
    $currentDll = $null

    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^\s*DLL Name:\s+(.+)$') {
            $currentDll = $Matches[1].Trim()
            if (-not $imports.Contains($currentDll)) {
                $imports[$currentDll] = New-Object System.Collections.Generic.List[string]
            }
            continue
        }

        if ($null -ne $currentDll -and $line -match '^\s*[0-9A-Fa-f]+\s+<none>\s+[0-9A-Fa-f]+\s+([^\s]+)\s*$') {
            [void]$imports[$currentDll].Add($Matches[1].Trim())
        }
    }

    return $imports
}

function Get-ManagedHostLogExpectedPeImports {
    return [ordered]@{
        "ADVAPI32.dll" = @(
            "RegisterEventSourceW",
            "ReportEventW",
            "DeregisterEventSource"
        )
        "bcrypt.dll" = @(
            "BCryptGenRandom"
        )
        "KERNEL32.dll" = @(
            "CloseHandle",
            "CreateEventExW",
            "DuplicateHandle",
            "FormatMessageW",
            "GetConsoleOutputCP",
            "GetCurrentProcess",
            "GetCurrentProcessorNumberEx",
            "GetCurrentThread",
            "GetEnvironmentVariableW",
            "GetLastError",
            "GetModuleFileNameW",
            "GetStdHandle",
            "GetThreadPriority",
            "GetTickCount64",
            "IsDebuggerPresent",
            "LocalFree",
            "MultiByteToWideChar",
            "QueryPerformanceCounter",
            "QueryPerformanceFrequency",
            "RaiseFailFastException",
            "SetEvent",
            "SetLastError",
            "Sleep",
            "VirtualAlloc",
            "VirtualFree",
            "WaitForMultipleObjectsEx",
            "WideCharToMultiByte",
            "WriteFile",
            "RtlCaptureContext",
            "FlsGetValue",
            "FlsSetValue",
            "SwitchToThread",
            "GetCurrentThreadId",
            "VirtualQuery",
            "EnterCriticalSection",
            "LeaveCriticalSection"
        )
        "ole32.dll" = @(
            "CoGetApartmentType",
            "CoInitializeEx",
            "CoUninitialize",
            "CoWaitForMultipleHandles"
        )
        "api-ms-win-crt-heap-l1-1-0.dll" = @(
            "free",
            "_callnewh",
            "malloc"
        )
    }
}

function Assert-ManagedHostLogSetEquals([string[]]$Actual, [string[]]$Expected, [string]$Label) {
    $actualSet = @($Actual | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Sort-Object -Unique)
    $expectedSet = @($Expected | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Sort-Object -Unique)
    $diff = Compare-Object -ReferenceObject $expectedSet -DifferenceObject $actualSet
    if ($diff) {
        throw "$Label mismatch.`nExpected: $($expectedSet -join ', ')`nActual: $($actualSet -join ', ')"
    }
}

function Assert-ManagedHostLogFileContains([string]$Path, [string[]]$Patterns, [string]$Label) {
    $text = Get-Content -LiteralPath $Path -Raw
    foreach ($pattern in $Patterns) {
        if ($text -notmatch $pattern) {
            throw "$Label missing pattern: $pattern"
        }
    }
}

function Assert-ManagedHostLogFileNotContains([string]$Path, [string[]]$Patterns, [string]$Label) {
    $text = Get-Content -LiteralPath $Path -Raw
    foreach ($pattern in $Patterns) {
        if ($text -match $pattern) {
            throw "$Label unexpectedly contained pattern: $pattern"
        }
    }
}

function Get-ManagedHostLogUInt16([byte[]]$Bytes, [int]$Offset) {
    return [BitConverter]::ToUInt16($Bytes, $Offset)
}

function Get-ManagedHostLogUInt32([byte[]]$Bytes, [int]$Offset) {
    return [BitConverter]::ToUInt32($Bytes, $Offset)
}

function Get-ManagedHostLogUInt64([byte[]]$Bytes, [int]$Offset) {
    return [BitConverter]::ToUInt64($Bytes, $Offset)
}

function Read-ManagedHostLogElfEnvelope([string]$ElfPath) {
    $bytes = [System.IO.File]::ReadAllBytes($ElfPath)
    if ($bytes.Length -lt 64) { throw "ELF is shorter than an ELF64 header: $ElfPath" }
    if ($bytes[0] -ne 0x7f -or $bytes[1] -ne 0x45 -or $bytes[2] -ne 0x4c -or $bytes[3] -ne 0x46) { throw "ELF magic is invalid: $ElfPath" }
    if ($bytes[4] -ne 2) { throw "Expected ELF64 class." }
    if ($bytes[5] -ne 1) { throw "Expected little-endian ELF." }
    if ([BitConverter]::IsLittleEndian -eq $false) { throw "The assertion host is not little-endian." }

    $type = Get-ManagedHostLogUInt16 $bytes 16
    $machine = Get-ManagedHostLogUInt16 $bytes 18
    $entry = Get-ManagedHostLogUInt64 $bytes 24
    $programHeaderOffset = Get-ManagedHostLogUInt64 $bytes 32
    $sectionHeaderOffset = Get-ManagedHostLogUInt64 $bytes 40
    $programHeaderSize = Get-ManagedHostLogUInt16 $bytes 54
    $programHeaderCount = Get-ManagedHostLogUInt16 $bytes 56
    $sectionHeaderCount = Get-ManagedHostLogUInt16 $bytes 60

    if ($type -ne 2) { throw "Expected ET_EXEC (2), got $type." }
    if ($machine -ne 0x3e) { throw "Expected AMD64 (0x3e), got 0x{0:X}." -f $machine }
    if ($programHeaderSize -ne 56) { throw "Expected ELF64 program-header size 56, got $programHeaderSize." }
    if ($programHeaderCount -eq 0) { throw "ELF has no program headers." }
    if ($sectionHeaderOffset -ne 0 -or $sectionHeaderCount -ne 0) { throw "Expected a sectionless proof ELF." }

    $phTableBytes = [uint64]$programHeaderSize * [uint64]$programHeaderCount
    if ($programHeaderOffset -gt [uint64]$bytes.Length -or $phTableBytes -gt [uint64]$bytes.Length - $programHeaderOffset) {
        throw "ELF program-header table exceeds the file."
    }

    $headers = New-Object System.Collections.Generic.List[object]
    for ($i = 0; $i -lt $programHeaderCount; $i++) {
        $offset = [int]($programHeaderOffset + ([uint64]$i * [uint64]$programHeaderSize))
        $header = [pscustomobject]@{
            Type = Get-ManagedHostLogUInt32 $bytes $offset
            Flags = Get-ManagedHostLogUInt32 $bytes ($offset + 4)
            Offset = Get-ManagedHostLogUInt64 $bytes ($offset + 8)
            VirtualAddress = Get-ManagedHostLogUInt64 $bytes ($offset + 16)
            FileSize = Get-ManagedHostLogUInt64 $bytes ($offset + 32)
            MemorySize = Get-ManagedHostLogUInt64 $bytes ($offset + 40)
            Align = Get-ManagedHostLogUInt64 $bytes ($offset + 48)
        }
        [void]$headers.Add($header)
    }

    $programHeaders = @($headers.ToArray())
    $loadSegments = @($programHeaders | Where-Object { $_.Type -eq 1 })
    $interpreterHeaders = @($programHeaders | Where-Object { $_.Type -eq 3 })
    $dynamicHeaders = @($programHeaders | Where-Object { $_.Type -eq 2 })
    $hasInterpreter = $interpreterHeaders.Count -gt 0
    $hasDynamic = $dynamicHeaders.Count -gt 0

    return [pscustomobject]@{
        Bytes = $bytes
        Type = $type
        Machine = $machine
        Entry = $entry
        ProgramHeaderCount = $programHeaderCount
        ProgramHeaders = $programHeaders
        LoadSegments = $loadSegments
        HasInterpreter = $hasInterpreter
        HasDynamic = $hasDynamic
    }
}

function Get-ManagedHostLogPeImageEnvelope([string]$PePath) {
    $bytes = [System.IO.File]::ReadAllBytes($PePath)
    if ($bytes.Length -lt 0x40 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) { throw "Published output is not a PE image." }
    $peOffset = [int](Get-ManagedHostLogUInt32 $bytes 0x3c)
    if ($peOffset -lt 0 -or $peOffset + 24 -gt $bytes.Length -or $bytes[$peOffset] -ne 0x50 -or $bytes[$peOffset + 1] -ne 0x45) { throw "Published PE signature is invalid." }
    $optionalHeaderOffset = $peOffset + 4 + 20
    if ((Get-ManagedHostLogUInt16 $bytes $optionalHeaderOffset) -ne 0x20b) { throw "Published output is not PE32+." }
    return [pscustomobject]@{
        ImageBase = Get-ManagedHostLogUInt64 $bytes ($optionalHeaderOffset + 24)
        SizeOfImage = Get-ManagedHostLogUInt32 $bytes ($optionalHeaderOffset + 56)
    }
}

function Assert-ManagedHostLogElfEnvelope(
    [string]$ElfPath,
    [string]$PePath,
    [string]$PeDumpPath,
    [string]$MapPath,
    [string]$NativeObjectDumpPath,
    [string]$ElfReadelfPath,
    [string]$ElfDumpPath,
    [string]$RuntimeSupportSourcePath,
    [switch]$GuideXosRuntimePack,
    [switch]$ManagedAllocation,
    [switch]$RepeatedAllocation) {
    $elf = Read-ManagedHostLogElfEnvelope $ElfPath
    $pe = Get-ManagedHostLogPeImageEnvelope $PePath
    $managedMain = Get-ManagedHostLogMapSymbolAddress $MapPath "ManagedMain" "Managed entry"
    $null = Assert-ManagedHostLogReversePInvokeChain $ElfPath $MapPath $PeDumpPath -GuideXosRuntimePack:$GuideXosRuntimePack -ManagedAllocation:$ManagedAllocation -RepeatedAllocation:$RepeatedAllocation

    if ($elf.Entry -ne $managedMain) { throw ("ELF entry 0x{0:X} is not ManagedMain 0x{1:X}." -f $elf.Entry, $managedMain) }
    if ($elf.LoadSegments.Count -eq 0) { throw "ELF has no PT_LOAD segments." }
    $first = $elf.LoadSegments[0]
    if ($first.VirtualAddress -ne $pe.ImageBase) { throw ("First PT_LOAD base 0x{0:X} does not match PE image base 0x{1:X}." -f $first.VirtualAddress, $pe.ImageBase) }
    if ($first.Offset -ne 0 -or $first.FileSize -ne 0 -or $first.MemorySize -ne 0x1000 -or $first.Flags -ne 4 -or $first.Align -ne 0x1000) {
        throw "The first PT_LOAD is not the read-only image-base reservation page."
    }

    $imageEnd = [uint64]$pe.ImageBase + [uint64]$pe.SizeOfImage
    $hasBss = $false
    $entryInLoad = $false
    foreach ($segment in $elf.LoadSegments) {
        if ($segment.MemorySize -eq 0) { throw "PT_LOAD has zero memory size." }
        if ($segment.FileSize -gt $segment.MemorySize) { throw "PT_LOAD file size exceeds memory size." }
        if ($segment.Align -ne 0x1000 -or ($segment.VirtualAddress % 0x1000) -ne 0) { throw "PT_LOAD is not page aligned." }
        if ($segment.Offset -gt [uint64]$elf.Bytes.Length -or $segment.FileSize -gt [uint64]$elf.Bytes.Length - $segment.Offset) { throw "PT_LOAD file range exceeds ELF." }
        $segmentEnd = $segment.VirtualAddress + $segment.MemorySize
        if ($segmentEnd -lt $segment.VirtualAddress -or $segment.VirtualAddress -lt $pe.ImageBase -or $segmentEnd -gt $imageEnd) { throw "PT_LOAD lies outside the PE image range." }
        if (($segment.Flags -band 2) -ne 0 -and ($segment.Flags -band 1) -ne 0) { throw "Writable-executable PT_LOAD is forbidden." }
        if ($segment.FileSize -lt $segment.MemorySize -and ($segment.Flags -band 2) -ne 0) { $hasBss = $true }
        if ($elf.Entry -ge $segment.VirtualAddress -and $elf.Entry -lt $segmentEnd) { $entryInLoad = $true }
    }
    if (-not $hasBss) { throw "Expected writable BSS/zero-fill representation was not found." }
    if (-not $entryInLoad) { throw "ELF entry is outside all PT_LOAD segments." }
    if ($elf.HasInterpreter) { throw "PT_INTERP is forbidden." }
    if ($elf.HasDynamic) { throw "PT_DYNAMIC/NEEDED dependencies are forbidden." }

    Assert-ManagedHostLogFileContains $ElfReadelfPath @(
        'There is no dynamic section in this file\.',
        'There are no relocations in this file\.',
        'There are no sections in this file\.'
    ) "ELF dependency and relocation envelope"
    Assert-ManagedHostLogFileNotContains $ElfReadelfPath @('NEEDED', 'PT_INTERP', 'rwx') "ELF dependency scan"

    if ($ManagedAllocation) {
        if ($RepeatedAllocation) {
            Assert-ManagedHostLogFileContains $NativeObjectDumpPath @(
                'HostLogProof_HostLogProof_Program__ManagedMain>',
                'guideXosManagedAllocationCanFit',
                'guideXosManagedAllocationValidateObject',
                'guideXosManagedAllocationReport'
            ) "Repeated managed allocation and proof-helper call evidence"
            Assert-ManagedHostLogFileContains $MapPath @(
                'guideXosManagedAllocationCanFit\s+[0-9A-Fa-f]{16}',
                'guideXosManagedAllocationValidateObject\s+[0-9A-Fa-f]{16}',
                'guideXosManagedAllocationReport\s+[0-9A-Fa-f]{16}'
            ) "Repeated managed allocation helper map evidence"
        } else {
            Assert-ManagedHostLogFileContains $NativeObjectDumpPath @(
                'HostLogProof_HostLogProof_Program__ManagedMain>',
                'movups\s+\(%rcx\),%xmm0',
                'movups\s+%xmm0,0x10\(%rsi\)',
                'call\s+[0-9A-Fa-f]+\s+<HostLogProof_HostLogProof_Program__GuideXosManagedArrayHostLog>'
            ) "Managed array population and opaque helper call evidence"
            Assert-ManagedHostLogFileContains $MapPath @(
                'guideXosManagedArrayHostLog\s+[0-9A-Fa-f]{16}',
                '__pinvoke_HostLogProof__Module____Internal__guideXosManagedArrayHostLog__Ansi'
            ) "Managed array host-helper binding evidence"
        }
    } else {
        Assert-ManagedHostLogFileContains $NativeObjectDumpPath @(
            'HostLogProof_HostLogProof_Program__ManagedMain>',
            'mov\s+0x8\(%rbx\),%rcx',
            'mov\s+0x8\(%rcx\),%rsi',
            'call\s+\*%rsi'
        ) "ManagedMain host callback evidence"
    }
    Assert-ManagedHostLogFileContains $MapPath @(
        'ManagedMain\s+[0-9A-Fa-f]{16}',
        'HostLogProof__Module___MainMethodWrapper',
        'HostLogProof__Module___StartupCodeMain',
        '_tls_index\s+[0-9A-Fa-f]{16}',
        '_tls_start\s+[0-9A-Fa-f]{16}',
        '_tls_end\s+[0-9A-Fa-f]{16}'
    ) "Managed map evidence"
    Assert-ManagedHostLogFileContains $ElfDumpPath @('flags r-x', 'flags rw-') "ELF segment permissions"
    Assert-ManagedHostLogFileNotContains $RuntimeSupportSourcePath @('Hello from managed guideXOS code', 'printf', 'puts', 'WriteFile') "native support output path"

    return $elf
}
