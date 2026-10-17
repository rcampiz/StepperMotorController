param(
    [ValidateSet("Upgrade", "Restore")]
    [string]$Mode = "Upgrade",

    [string]$ToolPath = ""
)

$downloadUrls = @{
    StLinkDrivers = "https://www.st.com/en/development-tools/stsw-link009.html"
    JLinkPack     = "https://www.segger.com/downloads/jlink/"
    StLinkReflash = "https://www.segger.com/downloads/jlink#STLinkReflash"
}

function Resolve-StLinkReflashPath {
    param([string]$ExplicitPath)

    if ($ExplicitPath -and (Test-Path -LiteralPath $ExplicitPath)) {
        return (Resolve-Path -LiteralPath $ExplicitPath).Path
    }

    $candidates = @(
        Join-Path $env:ProgramFiles "SEGGER\STLinkReflash\STLinkReflash.exe"
        Join-Path ${env:ProgramFiles(x86)} "SEGGER\STLinkReflash\STLinkReflash.exe"
        Join-Path $env:ProgramFiles "SEGGER\JLink\STLinkReflash.exe"
        Join-Path ${env:ProgramFiles(x86)} "SEGGER\JLink\STLinkReflash.exe"
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }

    if ($candidates.Count -gt 0) {
        return $candidates[0]
    }

    return $null
}

Write-Host "ST-LINK on-board -> J-Link OB conversion helper"
Write-Host "Mode: $Mode"
Write-Host ""
Write-Host "Prereqs:"
Write-Host "  - ST-LINK USB drivers (see: $($downloadUrls.StLinkDrivers))"
Write-Host "  - SEGGER J-Link Software Pack (see: $($downloadUrls.JLinkPack))"
Write-Host "  - SEGGER STLinkReflash utility (see: $($downloadUrls.StLinkReflash))"
Write-Host ""

$tool = Resolve-StLinkReflashPath -ExplicitPath $ToolPath

if (-not $tool) {
    Write-Host "STLinkReflash.exe not found."
    Write-Host "Install the utility, or pass -ToolPath to this script."
    exit 1
}

Write-Host "Found STLinkReflash: $tool"
Write-Host ""
Write-Host "Next steps (manual in GUI):"
if ($Mode -eq "Upgrade") {
    Write-Host "  1) Connect the on-board ST-LINK to USB."
    Write-Host "  2) In STLinkReflash, select 'Upgrade to J-Link'."
    Write-Host "  3) Wait for completion, then close the utility."
} else {
    Write-Host "  1) Connect the on-board ST-LINK to USB."
    Write-Host "  2) In STLinkReflash, select 'Restore ST-Link'."
    Write-Host "  3) Wait for completion, then close the utility."
}
Write-Host ""
Write-Host "Launching STLinkReflash..."
Start-Process -FilePath $tool
