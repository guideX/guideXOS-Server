param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"
Set-Location -LiteralPath $Root

$outputDir = Join-Path $Root "tmp\phase8q-jpeg-codec-test"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$exe = Join-Path $outputDir "jpeg_codec_test.exe"
$build = & cmd.exe /c "g++ -std=c++17 -O2 -iquote . tests\jpeg_codec_test.cpp jpeg_loader.cpp image_adapter.cpp image.cpp image_renderer.cpp png_loader.cpp vfs.cpp logger.cpp -lgdi32 -lmsimg32 -o `"$exe`" 2>&1"
if ($LASTEXITCODE -ne 0) {
    $build | Write-Output
    throw "JPEG codec test build failed."
}

& $exe
if ($LASTEXITCODE -ne 0) {
    throw "JPEG codec tests failed."
}

Write-Output "JPEG codec smoke PASS"
