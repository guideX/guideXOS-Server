<#
.SYNOPSIS
    Convert the original Missile Command build1.gif city art to the
    app-visible GXIM resource used by the MC4 port.

.DESCRIPTION
    Reads the project-owned VB6 original
    D:\dev\bkup\inactive\missilecommand\build1.gif (153x121, 8-bit indexed,
    single frame, no transparency) via System.Drawing and writes
    sdk/samples/missilecommand/resources/city.gximg in the exact 28-byte
    app-visible GXIM layout that D:\dev\pacman\guidexos\tools\
    convert_bmp_to_gximg.ps1 produces for the PacMan sprites:

      offset  0: "GXIM" magic (4 bytes)
      offset  4: uint32 version = 1
      offset  8: uint32 width
      offset 12: uint32 height
      offset 16: uint32 strideBytes (= width * 4)
      offset 20: uint32 pixelFormat = 1 (GX_PIXEL_FORMAT_XRGB8888)
      offset 24: uint32 payloadBytes (= stride * height)
      offset 28: pixels top-down, 4 bytes each: B, G, R, 0
                (little-endian uint32 0x00RRGGBB, XRGB8888)

    The game treats pixel value 0x00000000 (source black) as transparent so
    the playfield background shows through; every other palette entry is
    opaque. The conversion is deterministic: no resize, no dither, no color
    adjustment -- GetPixel values are copied verbatim top-down.

    Verification: an independent pure byte-level GIF decode of build1.gif
    (LZW, no image library) must agree on dimensions (153x121), palette
    usage (10 indices, black background majority), and per-pixel indices.
    The host test tests/missilecommand_state_test.cpp re-validates the
    staged city.gximg header, dimensions, payload size, and color content.
#>

[CmdletBinding()]
param(
    [string]$Source = "D:\dev\bkup\inactive\missilecommand\build1.gif",
    [string]$Destination = ""
)

$ErrorActionPreference = "Stop"

$ServerRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($Destination)) {
    $Destination = Join-Path $ServerRoot "sdk\samples\missilecommand\resources\city.gximg"
}

if (!(Test-Path -LiteralPath $Source -PathType Leaf)) {
    throw "Missing original city art: $Source"
}

Add-Type -AssemblyName System.Drawing
$sourceBitmap = [System.Drawing.Bitmap]::FromFile($Source)
try {
    $width = $sourceBitmap.Width
    $height = $sourceBitmap.Height
    if ($width -ne 153 -or $height -ne 121) {
        throw "Unexpected build1.gif dimensions: ${width}x${height} (expected 153x121)"
    }

    $strideBytes = $width * 4
    $payloadBytes = $strideBytes * $height
    $output = New-Object byte[] (28 + $payloadBytes)
    $output[0] = [byte][char]'G'; $output[1] = [byte][char]'X'
    $output[2] = [byte][char]'I'; $output[3] = [byte][char]'M'
    [Array]::Copy([BitConverter]::GetBytes([uint32]1), 0, $output, 4, 4)
    [Array]::Copy([BitConverter]::GetBytes([uint32]$width), 0, $output, 8, 4)
    [Array]::Copy([BitConverter]::GetBytes([uint32]$height), 0, $output, 12, 4)
    [Array]::Copy([BitConverter]::GetBytes([uint32]$strideBytes), 0, $output, 16, 4)
    [Array]::Copy([BitConverter]::GetBytes([uint32]1), 0, $output, 20, 4)
    [Array]::Copy([BitConverter]::GetBytes([uint32]$payloadBytes), 0, $output, 24, 4)

    for ($y = 0; $y -lt $height; $y++) {
        for ($x = 0; $x -lt $width; $x++) {
            $pixel = $sourceBitmap.GetPixel($x, $y)
            $destinationPixel = 28 + $y * $strideBytes + $x * 4
            $output[$destinationPixel] = $pixel.B
            $output[$destinationPixel + 1] = $pixel.G
            $output[$destinationPixel + 2] = $pixel.R
            $output[$destinationPixel + 3] = 0
        }
    }

    $parent = Split-Path -Parent $Destination
    if (-not (Test-Path $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    [IO.File]::WriteAllBytes($Destination, $output)
    Write-Output ("Converted {0} -> {1} ({2}x{3}, {4} bytes)" -f $Source, $Destination, $width, $height, $output.Length)
}
finally {
    $sourceBitmap.Dispose()
}
