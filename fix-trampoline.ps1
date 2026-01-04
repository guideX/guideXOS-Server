#
# Fix Bootloader Build - Enable trampoline_msvc.cpp
#
# The trampoline_msvc.cpp file is currently excluded from the build.
# This script removes that exclusion so the trampoline functions are compiled.
#

$ErrorActionPreference = "Stop"

$projectFile = "guideXOSBootLoader\guideXOSBootLoader.vcxproj"

Write-Host "Fixing bootloader project..." -ForegroundColor Cyan
Write-Host ""

if (!(Test-Path $projectFile)) {
    Write-Host "ERROR: Project file not found at: $projectFile" -ForegroundColor Red
    exit 1
}

# Read the project file
$content = Get-Content $projectFile -Raw

# Check if trampoline_msvc.cpp is excluded
if ($content -match '<ClCompile Include="trampoline_msvc\.cpp">\s*<ExcludedFromBuild>true</ExcludedFromBuild>\s*</ClCompile>') {
    Write-Host "Found excluded trampoline_msvc.cpp - enabling it..." -ForegroundColor Yellow
    
    # Replace the excluded entry with a normal one
    $content = $content -replace '<ClCompile Include="trampoline_msvc\.cpp">\s*<ExcludedFromBuild>true</ExcludedFromBuild>\s*</ClCompile>',
                                  '<ClCompile Include="trampoline_msvc.cpp" />'
    
    # Backup the original
    $backupFile = "$projectFile.backup"
    Copy-Item $projectFile $backupFile -Force
    Write-Host "Created backup at: $backupFile" -ForegroundColor Gray
    
    # Save the modified project
    Set-Content $projectFile -Value $content -NoNewline
    
    Write-Host "SUCCESS: trampoline_msvc.cpp is now enabled!" -ForegroundColor Green
    Write-Host ""
    Write-Host "Next steps:" -ForegroundColor Cyan
    Write-Host "  1. Close Visual Studio (if open)" -ForegroundColor White
    Write-Host "  2. Reopen the solution" -ForegroundColor White
    Write-Host "  3. Run: .\build-uefi.ps1" -ForegroundColor White
    Write-Host ""
}
elseif ($content -match '<ClCompile Include="trampoline_msvc\.cpp"\s*/>') {
    Write-Host "trampoline_msvc.cpp is already enabled!" -ForegroundColor Green
    Write-Host ""
    Write-Host "If build still fails, try:" -ForegroundColor Yellow
    Write-Host "  1. Clean solution: Remove-Item 'guideXOSBootLoader\guideXOS.1fedf2ad' -Recurse -Force" -ForegroundColor White
    Write-Host "  2. Rebuild: .\build-uefi.ps1" -ForegroundColor White
    Write-Host ""
}
else {
    Write-Host "WARNING: Could not find trampoline_msvc.cpp entry in project file" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Checking project file structure..." -ForegroundColor Cyan
    
    # Show relevant section
    $lines = Get-Content $projectFile
    $inItemGroup = $false
    foreach ($line in $lines) {
        if ($line -match '<ItemGroup>') {
            $inItemGroup = $true
        }
        if ($inItemGroup -and $line -match 'ClCompile|trampoline') {
            Write-Host "  $line" -ForegroundColor Gray
        }
        if ($line -match '</ItemGroup>') {
            $inItemGroup = $false
        }
    }
}
