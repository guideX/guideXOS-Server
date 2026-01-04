# Fix guideXOSBootLoader project
$projectFile = "guideXOSBootLoader\guideXOSBootLoader.vcxproj"

if (Test-Path $projectFile) {
    $content = Get-Content $projectFile -Raw
    
    # Check if trampoline_msvc.cpp is already in the project
    if ($content -notmatch "trampoline_msvc\.cpp") {
        Write-Host "Adding trampoline_msvc.cpp to project..." -ForegroundColor Yellow
        
        # Find the ItemGroup with ClCompile entries
        $content = $content -replace '(<ClCompile Include="main.cpp" />)', 
            '$1`n    <ClCompile Include="trampoline_msvc.cpp" />'
        
        # Save the file
        Set-Content $projectFile -Value $content
        Write-Host "Done! trampoline_msvc.cpp added to project" -ForegroundColor Green
    } else {
        Write-Host "trampoline_msvc.cpp is already in the project" -ForegroundColor Green
    }
} else {
    Write-Host "ERROR: Project file not found at $projectFile" -ForegroundColor Red
}