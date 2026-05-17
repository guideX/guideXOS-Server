param(
    [string]$Size = "48",
    [string]$InputRoot = "assets/Images/Flat",
    [string]$Output = "kernel/core/include/kernel/desktop_icon_theme_flat.h"
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

$icons = @(
    @{ Symbol = "Notepad"; File = "27-Edit_Text_256x256_35395.png" },
    @{ Symbol = "Calculator"; File = "15-Dashboard__256x256_35400.png" },
    @{ Symbol = "Clock"; File = "47-_iCal_256x256_35384.png" },
    @{ Symbol = "Console"; File = "29-Generic_256x256_35387.png" },
    @{ Symbol = "Files"; File = "25-Folder_256x256_35390.png" },
    @{ Symbol = "FileGeneric"; File = "31-Document_256x256_35398.png" },
    @{ Symbol = "Paint"; File = "7-Image_capture_256x256_35382.png" },
    @{ Symbol = "TaskManager"; File = "15-Dashboard__256x256_35400.png" },
    @{ Symbol = "TrashEmpty"; File = "24-Empty_Trash_256x256_35394.png" },
    @{ Symbol = "TrashFull"; File = "23-Full_Trash_256x256_35388.png" }
)

$sizeFolder = Join-Path $InputRoot $Size
if (!(Test-Path $sizeFolder)) {
    throw "Flat icon size folder not found: $sizeFolder"
}

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add('#pragma once')
$lines.Add("// Auto-generated from assets/Images/Flat/$Size PNG icons (ARGB format)")
$lines.Add('#include <stdint.h>')
$lines.Add("static const uint32_t kDesktopThemeIconW = $Size;")
$lines.Add("static const uint32_t kDesktopThemeIconH = $Size;")
$lines.Add('')

foreach ($icon in $icons) {
    $path = Join-Path $sizeFolder $icon.File
    if (!(Test-Path $path)) {
        throw "Icon source not found: $path"
    }

    $bitmap = [System.Drawing.Bitmap]::FromFile($path)
    try {
        if ($bitmap.Width -ne [int]$Size -or $bitmap.Height -ne [int]$Size) {
            throw "Unexpected bitmap size for $path : $($bitmap.Width)x$($bitmap.Height)"
        }

        $symbol = "kDesktopThemeIcon_{0}" -f $icon.Symbol
        $pixelCount = $bitmap.Width * $bitmap.Height
        $lines.Add("static const uint32_t $symbol[$pixelCount] = {")

        $row = New-Object System.Collections.Generic.List[string]
        for ($y = 0; $y -lt $bitmap.Height; $y++) {
            for ($x = 0; $x -lt $bitmap.Width; $x++) {
                $c = $bitmap.GetPixel($x, $y)
                $argb = ('0x{0:X8}' -f $c.ToArgb())
                $row.Add($argb)
                if ($row.Count -eq 8) {
                    $lines.Add('    ' + ($row -join ', ') + ', ')
                    $row.Clear()
                }
            }
        }

        if ($row.Count -gt 0) {
            $lines.Add('    ' + ($row -join ', ') + ', ')
        }

        $lines.Add('};')
        $lines.Add('')
    }
    finally {
        $bitmap.Dispose()
    }
}

[System.IO.File]::WriteAllLines((Resolve-Path (Split-Path $Output -Parent)).Path + [System.IO.Path]::DirectorySeparatorChar + [System.IO.Path]::GetFileName($Output), $lines)
Write-Host "Generated $Output from $sizeFolder"
