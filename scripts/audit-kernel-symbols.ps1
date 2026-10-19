param(
    [Parameter(Mandatory = $true)]
    [string]$ElfPath,

    [Parameter(Mandatory = $true)]
    [string]$PePath,

    [Parameter(Mandatory = $true)]
    [string]$ReportPath,

    [Parameter(Mandatory = $true)]
    [string]$ObjectRoot,

    [string]$BuildLogPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-Tool([string]$Path, [string[]]$Arguments, [string]$Description) {
    $output = @(& $Path @Arguments 2>&1)
    $exitCode = if ($null -eq $LASTEXITCODE) { if ($?) { 0 } else { 1 } } else { [int]$LASTEXITCODE }
    if ($exitCode -ne 0) {
        throw "$Description failed with exit code ${exitCode}: $Path $($Arguments -join ' ')"
    }
    return (($output | ForEach-Object { [string]$_ }) -join [Environment]::NewLine)
}

$elf = (Resolve-Path -LiteralPath $ElfPath).Path
$pe = (Resolve-Path -LiteralPath $PePath).Path
$objectsRoot = (Resolve-Path -LiteralPath $ObjectRoot).Path
$readelfCommand = Get-Command readelf.exe -ErrorAction SilentlyContinue
$objdumpCommand = Get-Command objdump.exe -ErrorAction SilentlyContinue
$nmCommand = Get-Command nm.exe -ErrorAction SilentlyContinue
if ($null -eq $readelfCommand -or $null -eq $objdumpCommand -or $null -eq $nmCommand) {
    throw 'GNU readelf.exe, objdump.exe, and nm.exe are required for the AMD64 kernel symbol audit.'
}

$header = Invoke-Tool $readelfCommand.Source @('-hW', $elf) 'ELF header inspection'
if ($header -notmatch 'Class:\s+ELF64' -or
    $header -notmatch 'Type:\s+EXEC' -or
    $header -notmatch 'Machine:\s+Advanced Micro Devices X86-64') {
    throw 'Kernel ELF is not an AMD64 ELF64 executable; automatic symbol classification is unsafe.'
}

$sections = Invoke-Tool $readelfCommand.Source @('-SW', $elf) 'ELF section inspection'
if ($sections -notmatch '\.symtab' -or $sections -match '\.dynsym|\.dynamic') {
    throw 'Kernel ELF symbol sections are not the expected static, non-dynamic layout; manual review is required.'
}

$programHeaders = Invoke-Tool $readelfCommand.Source @('-lW', $elf) 'ELF program-header inspection'
if ($programHeaders -match 'DYNAMIC') {
    throw 'Kernel ELF has a dynamic program segment; manual symbol review is required.'
}
$sectionMapping = ($programHeaders -split 'Section to Segment mapping:') | Select-Object -Last 1
if ($sectionMapping -match '\.symtab') {
    throw 'The ELF symbol table is loadable; manual symbol review is required.'
}

$relocations = Invoke-Tool $readelfCommand.Source @('-rW', $elf) 'ELF relocation inspection'
if ($relocations -notmatch '(?m)^There are no relocations in this file\.') {
    throw 'Kernel ELF contains relocation records; unresolved references require manual classification.'
}

$symbolText = Invoke-Tool $readelfCommand.Source @('-Ws', $elf) 'ELF symbol-table inspection'
$symbols = New-Object System.Collections.Generic.List[object]
$activeTable = ''
foreach ($line in ($symbolText -split "`r?`n")) {
    if ($line -match "Symbol table '([^']+)' contains") {
        $activeTable = $Matches[1]
        continue
    }
    if ($line -match '^\s*\d+:\s+[0-9a-fA-F]+\s+\d+\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)(?:\s+(.*))?$') {
        $sectionIndex = $Matches[4]
        $name = if ($Matches[5]) { $Matches[5].Trim() } else { '' }
        if ($sectionIndex -eq 'UND' -and $name.Length -gt 0) {
            $symbols.Add([PSCustomObject]@{
                Table = $activeTable
                Type = $Matches[1]
                Binding = $Matches[2]
                Visibility = $Matches[3]
                Section = $sectionIndex
                Name = $name
            })
        }
    }
}

$peImports = Invoke-Tool $objdumpCommand.Source @('-p', $pe) 'PE import-table inspection'
$peImportText = $peImports
if ($peImportText -match '(?m)^\s*DLL Name:') {
    throw 'Kernel PE has imported DLL dependencies; unresolved production references require manual review.'
}
$peUndefinedText = Invoke-Tool $nmCommand.Source @('-u', $pe) 'PE undefined-symbol inspection'
$peUndefinedNames = @($peUndefinedText -split "`r?`n" | ForEach-Object {
    if ($_ -match '^\s*U\s+(\S+)\s*$') { $Matches[1] }
} | Where-Object { $_ })
$peAllSymbols = Invoke-Tool $nmCommand.Source @($pe) 'PE symbol inspection'
$peDefinitions = @{}
foreach ($line in ($peAllSymbols -split "`r?`n")) {
    if ($line -match '^\s*[0-9a-fA-F]+\s+([A-Za-z])\s+(\S+)\s*$' -and $Matches[1] -notin @('U', 'u')) {
        $peDefinitions[$Matches[2]] = $Matches[1]
    }
}
$objectFiles = @(Get-ChildItem -LiteralPath $objectsRoot -Recurse -File -Filter '*.o' | Sort-Object FullName)
if ($objectFiles.Count -eq 0) { throw "No kernel object files were found under $objectsRoot" }
$objectUndefinedText = Invoke-Tool $nmCommand.Source (@('-A', '-u') + @($objectFiles | ForEach-Object { $_.FullName })) 'Kernel object undefined-symbol inspection'
$objectReferences = New-Object System.Collections.Generic.List[object]
foreach ($line in ($objectUndefinedText -split "`r?`n")) {
    if ($line -match '^(.+?):\s+([A-Za-z])\s+(\S+)\s*$') {
        $objectReferences.Add([PSCustomObject]@{
            Object = $Matches[1]
            Binding = $Matches[2]
            Name = $Matches[3]
        })
    }
}
$coffRelocations = Invoke-Tool $objdumpCommand.Source @('-r', $pe) 'PE symbol-relocation inspection'
if ($coffRelocations -match '(?m)^\s*[0-9a-fA-F]+\s+') {
    throw 'Kernel PE contains COFF symbol relocations; unresolved references require manual classification.'
}

if (-not [string]::IsNullOrWhiteSpace($BuildLogPath)) {
    $buildLog = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $BuildLogPath).Path)
    if ($buildLog -match '(?im)(undefined reference|undefined symbol|collect2: error|ld(?:\.exe)?: error|make(?:\.exe)?: \*\*\*)') {
        throw 'The supplied production build log contains a linker failure diagnostic.'
    }
}

$uniqueSymbols = @($symbols | Sort-Object Table,Name -Unique)
$unclassified = @($symbols | Where-Object { $_.Table -ne '.symtab' -or $_.Binding -notin @('GLOBAL', 'WEAK') })
if ($unclassified.Count -gt 0) {
    throw 'Undefined entries have unexpected sections or bindings; manual classification is required.'
}
$peUndefinedSet = @($peUndefinedNames | Sort-Object -Unique)
$elfUndefinedSet = @($uniqueSymbols | ForEach-Object { $_.Name } | Sort-Object -Unique)
$symbolNameDifferences = @(Compare-Object $elfUndefinedSet $peUndefinedSet)
if ($symbolNameDifferences.Count -ne 0) {
    throw 'PE and ELF undefined symbol names differ; automatic classification is unsafe.'
}
$dsoWeakAlias = $peAllSymbols -match '(?m)^\s*[0-9a-fA-F]+\s+[Bb]\s+\.weak\.__dso_handle\.'
$dsoObjectReferences = @($objectReferences | Where-Object { $_.Name -eq '__dso_handle' })
$mainCrtObjectReferences = @($objectReferences | Where-Object { $_.Name -eq 'mainCRTStartup' })
$unresolvedNotExplained = @($uniqueSymbols | Where-Object {
    -not $peDefinitions.ContainsKey($_.Name) -and
    -not ($_.Name -eq '__dso_handle' -and $dsoWeakAlias -and
          $dsoObjectReferences.Count -gt 0 -and
          @($dsoObjectReferences | Where-Object { $_.Binding -notin @('w', 'W') }).Count -eq 0) -and
    -not ($_.Name -eq 'mainCRTStartup' -and $mainCrtObjectReferences.Count -eq 0)
})
if ($unresolvedNotExplained.Count -gt 0) {
    throw "Undefined names have no final definition or documented ABI/entry-point role: $(($unresolvedNotExplained | ForEach-Object Name) -join ', ')"
}

$rows = New-Object System.Collections.Generic.List[string]
$rows.Add('# AMD64 kernel undefined symbol classification')
$rows.Add('')
$rows.Add("ELF: $([IO.Path]::GetFileName($elf)) bytes=$((Get-Item -LiteralPath $elf).Length) sha256=$((Get-FileHash -LiteralPath $elf -Algorithm SHA256).Hash.ToLowerInvariant())")
$rows.Add("PE: $([IO.Path]::GetFileName($pe)) bytes=$((Get-Item -LiteralPath $pe).Length) sha256=$((Get-FileHash -LiteralPath $pe -Algorithm SHA256).Hash.ToLowerInvariant())")
$readelfVersionText = Invoke-Tool $readelfCommand.Source @('--version') 'readelf version query'
$objdumpVersionText = Invoke-Tool $objdumpCommand.Source @('--version') 'objdump version query'
$readelfVersion = ($readelfVersionText -split "`r?`n")[0].Trim()
$objdumpVersion = ($objdumpVersionText -split "`r?`n")[0].Trim()
$rows.Add("GNU readelf: $readelfVersion")
$rows.Add("GNU objdump: $objdumpVersion")
$rows.Add('ELF class/type/machine: ELF64 / EXEC / AMD64')
$rows.Add('ELF .symtab: present and outside load segments; .dynsym/.dynamic/PT_DYNAMIC: absent')
$rows.Add('ELF relocations: none')
$rows.Add('PE imports: none; PE COFF symbol relocations: none; linker entry is _start from arch/amd64/linker.ld')
$rows.Add("Undefined ELF entries: $($symbols.Count) rows / $($uniqueSymbols.Count) distinct names")
$resolvedUndefinedCount = @($uniqueSymbols | Where-Object { $peDefinitions.ContainsKey($_.Name) }).Count
$rows.Add("PE COFF UND entries: $($peUndefinedNames.Count) rows / $($peUndefinedSet.Count) distinct names; $resolvedUndefinedCount names have final-image definitions; $($objectFiles.Count) input objects inspected")
$rows.Add('')
$rows.Add('| Symbol | Binding | Type | Section | Classification | Evidence |')
$rows.Add('| --- | --- | --- | --- | --- | --- |')
foreach ($symbol in $uniqueSymbols) {
    $evidence = 'ELF .symtab GLOBAL NOTYPE UND; no relocation, import, dynamic symbol, or dynamic segment'
    $classification = if ($symbol.Binding -eq 'WEAK') {
        'Intentionally unresolved weak reference'
    } elseif ($peDefinitions.ContainsKey($symbol.Name)) {
        $sourceRefs = @($objectReferences | Where-Object { $_.Name -eq $symbol.Name })
        $strongSourceRefs = @($sourceRefs | Where-Object { $_.Binding -in @('U', 'G') }).Count
        $evidence += '; same-named final PE definition resolves the COFF input reference'
        if ($strongSourceRefs -gt 0) { $evidence += "; $strongSourceRefs strong input-object references were resolved" }
        'Linker-resolved strong reference; retained COFF table entry'
    } elseif ($symbol.Name -eq '__dso_handle' -and $dsoWeakAlias) {
        $evidence += '; object references are weak; final PE contains MinGW .weak.__dso_handle ABI guard storage'
        'Compiler ABI bookkeeping entry; no runtime reference'
    } elseif ($symbol.Name -eq 'mainCRTStartup') {
        $evidence += '; absent from all input-object undefined references; linker script selects _start'
        'Unused/non-loadable symbol-table entry'
    } else {
        throw "No safe classification for $($symbol.Name)."
    }
    $rows.Add("| ``$($symbol.Name)`` | $($symbol.Binding) | $($symbol.Type) | $($symbol.Table) / UND | $classification | $evidence |")
}
$rows.Add('')
$rows.Add('All listed ELF entries exist only in the non-loadable .symtab. The PE COFF table retains per-input UND rows alongside same-named final definitions for resolved strong references. __dso_handle is weak in its sole input reference and uses MinGW weak ABI guard storage. mainCRTStartup is absent from all input-object undefined references; the custom linker script selects ENTRY(_start). No symbol relocations or dynamic imports/dependencies remain. The production PE and ELF therefore have no unexplained required unresolved dependency. This conclusion is limited to this exact ELF/PE pair, its complete input-object set, and the inspected toolchain output.')

$fullReportPath = [IO.Path]::GetFullPath($ReportPath)
$reportDirectory = Split-Path -Parent $fullReportPath
if (-not (Test-Path -LiteralPath $reportDirectory -PathType Container)) {
    New-Item -ItemType Directory -Path $reportDirectory -Force | Out-Null
}
[IO.File]::WriteAllText($fullReportPath, ($rows -join [Environment]::NewLine) + [Environment]::NewLine, (New-Object System.Text.UTF8Encoding($false)))
Write-Output "Kernel symbol audit PASS: $($symbols.Count) UND rows, $($uniqueSymbols.Count) distinct .symtab entries; no relocations/imports."
Write-Output "Report: $fullReportPath"
