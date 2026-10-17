# PowerShell script to launch OpenOCD and auto-cleanup when it exits
# This ensures OpenOCD is always killed when debugging stops

param(
    [string]$ConfigArgs = "-f interface/stlink.cfg -f target/stm32f4x.cfg"
)

# Kill any existing OpenOCD instances
Get-Process openocd -ErrorAction SilentlyContinue | Stop-Process -Force

# Start OpenOCD
$openocdPath = "openocd"
$openocdArgs = $ConfigArgs -split " "

Write-Host "Starting OpenOCD..."
$openocdProcess = Start-Process -FilePath $openocdPath -ArgumentList $openocdArgs -PassThru -NoNewWindow

# Wait for OpenOCD to exit
$openocdProcess.WaitForExit()

Write-Host "OpenOCD terminated."
exit 0