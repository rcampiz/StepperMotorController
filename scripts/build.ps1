# Build firmware and/or host tests with CMake presets.
#   scripts\build.ps1                 firmware (Debug)
#   scripts\build.ps1 -Tests          host unit tests + ctest
#   scripts\build.ps1 -All            both
#   scripts\build.ps1 -Release        firmware Release (no interface trace)
#   scripts\build.ps1 -Clean          wipe the chosen build dirs first
[CmdletBinding()]
param([switch]$Tests, [switch]$All, [switch]$Release, [switch]$Clean)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'env.ps1')
Set-Location $repo

$doFw = (-not $Tests) -or $All
$doTests = $Tests -or $All

if ($doFw) {
    $preset = if ($Release) { 'firmware-release' } else { 'firmware-debug' }
    if ($Clean) { Remove-Item -Recurse -Force (Join-Path $repo ($(if ($Release) { 'build-release' } else { 'build' }))) -ErrorAction SilentlyContinue }
    cmake --preset $preset;            if ($LASTEXITCODE) { exit $LASTEXITCODE }
    cmake --build --preset $preset;    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}
if ($doTests) {
    if ($Clean) { Remove-Item -Recurse -Force (Join-Path $repo 'build-tests') -ErrorAction SilentlyContinue }
    Push-Location (Join-Path $repo 'tests')
    try {
        cmake --preset host-tests;           if ($LASTEXITCODE) { exit $LASTEXITCODE }
        cmake --build --preset host-tests;   if ($LASTEXITCODE) { exit $LASTEXITCODE }
        ctest --preset host-tests;           if ($LASTEXITCODE) { exit $LASTEXITCODE }
    } finally { Pop-Location }
}
