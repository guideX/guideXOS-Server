param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$runtimeSmoke = Join-Path $Root "scripts\smoke-imageviewer-baremetal-runtime.ps1"
if ($SkipBuild) {
    & $runtimeSmoke -SkipBuild -SmokeLabel "large-png" -AssetPath "/system/wall/arrowbgx.png" -FallbackPath "/system/wall/imageviewer-runtime-smoke-placeholder.png" -StrictLargePng
} else {
    & $runtimeSmoke -SmokeLabel "large-png" -AssetPath "/system/wall/arrowbgx.png" -FallbackPath "/system/wall/imageviewer-runtime-smoke-placeholder.png" -StrictLargePng
}
