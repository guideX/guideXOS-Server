param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$runtimeSmoke = Join-Path $Root "scripts\smoke-imageviewer-baremetal-runtime.ps1"
if ($SkipBuild) {
    & $runtimeSmoke -SkipBuild -SmokeLabel "close-reopen" -AssetPath "/system/wall/ameoba.png" -FallbackPath "/system/wall/imageviewer-runtime-smoke-placeholder.png" -CloseReopen
} else {
    & $runtimeSmoke -SmokeLabel "close-reopen" -AssetPath "/system/wall/ameoba.png" -FallbackPath "/system/wall/imageviewer-runtime-smoke-placeholder.png" -CloseReopen
}
