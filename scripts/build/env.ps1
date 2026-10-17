# Dot-source this to make freshly installed tools visible in the current PowerShell session:
#     . scripts\build\env.ps1
# winget installers update the registry PATH, not running shells. This merges Machine + User
# PATH into the session and adds winget's per-user portable-package bin folders.
$machine = [Environment]::GetEnvironmentVariable('Path', 'Machine')
$user    = [Environment]::GetEnvironmentVariable('Path', 'User')
$env:Path = "$machine;$user;$env:Path"

$pkgRoot = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
foreach ($pattern in 'BrechtSanders.WinLibs*', 'xpack-dev-tools.openocd*') {
    Get-ChildItem $pkgRoot -Directory -Filter $pattern -ErrorAction SilentlyContinue | ForEach-Object {
        Get-ChildItem $_.FullName -Recurse -Directory -Filter 'bin' -ErrorAction SilentlyContinue |
            Where-Object { (Test-Path (Join-Path $_.FullName 'g++.exe')) -or (Test-Path (Join-Path $_.FullName 'openocd.exe')) } |
            ForEach-Object { if ($env:Path -notlike "*$($_.FullName)*") { $env:Path = "$($_.FullName);$env:Path" } }
    }
}
