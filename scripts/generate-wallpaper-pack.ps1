param(
    [string]$InputDir = "assets/Backgrounds",
    [string]$OutputDir = "out/wallpaper-pack",
    [string]$OutputImage = "ESP/ramdisk.img",
    [switch]$LinuxImg
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

function Write-GximgFile {
    param(
        [string]$SourcePath,
        [string]$TargetPath
    )

    $bitmap = [System.Drawing.Bitmap]::FromFile($SourcePath)
    try {
        $fs = [System.IO.File]::Open($TargetPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
        try {
            $bw = New-Object System.IO.BinaryWriter($fs)
            $magic = [System.Text.Encoding]::ASCII.GetBytes("GXIMG001")
            $bw.Write($magic)
            $bw.Write([uint32]$bitmap.Width)
            $bw.Write([uint32]$bitmap.Height)
            $bw.Write([uint32]1)
            for ($y = 0; $y -lt $bitmap.Height; $y++) {
                for ($x = 0; $x -lt $bitmap.Width; $x++) {
                    $argb = [uint32]$bitmap.GetPixel($x, $y).ToArgb()
                    $bw.Write($argb)
                }
            }
            $bw.Flush()
        }
        finally {
            $fs.Dispose()
        }
    }
    finally {
        $bitmap.Dispose()
    }
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$wallpaperDir = Join-Path $OutputDir "Wallpapers"
New-Item -ItemType Directory -Force -Path $wallpaperDir | Out-Null

Get-ChildItem $InputDir -Filter *.png | ForEach-Object {
    $targetName = [System.IO.Path]::GetFileNameWithoutExtension($_.Name) + ".gximg"
    Write-GximgFile -SourcePath $_.FullName -TargetPath (Join-Path $wallpaperDir $targetName)
}

if ($LinuxImg) {
    $staging = Resolve-Path $OutputDir
    $imgPath = Join-Path (Get-Location) $OutputImage
    Write-Host "Generating FAT wallpaper image via Linux tooling..."
    Write-Host "Example command:"
    Write-Host "  bash -lc 'truncate -s 64M \"$imgPath\" && mkfs.vfat \"$imgPath\" && mmd -i \"$imgPath\" ::/Wallpapers && mcopy -i \"$imgPath\" $($staging.Path -replace '\\','/')/Wallpapers/* ::/Wallpapers/'"
} else {
    Write-Host "Generated GXIMG wallpaper files under $wallpaperDir"
    Write-Host "Use Linux mtools or mkfs.vfat to pack them into ramdisk.img at /Wallpapers"
}
