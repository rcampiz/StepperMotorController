<#
.SYNOPSIS
    PC <-> Raspberry Pi coordinator ("robot") over a direct Ethernet cable: one double-click to
    set up and connect. Started by Robot.bat (the menu) or "Pi Desktop.bat" (straight to VNC).

.DESCRIPTION
    First run, or menu [5]: the one-time setup
      1. an SSH key (~/.ssh/id_ed25519) and a "Host robot" entry in ~/.ssh/config
      2. the network link on the wired adapter: Internet Connection Sharing from the adapter that
         has internet (the Pi gets DHCP and internet through the cable), or with no internet,
         fixed addresses PC 192.168.50.1/24, Pi 192.168.50.2/24. Asked first; admin rights are
         asked for (UAC) only for this step. Undo from [5].
      3. finding the Pi: robot.local (mDNS), then the link's subnet and the ARP table for
         Raspberry Pi MAC prefixes
      4. the key copied to the Pi if needed (its password once), this PC's repository sent to the
         Pi as a git bundle, and scripts/pi/pi_setup.sh run there
    Every step checks first and changes only what is missing: running it twice is harmless.

    Windows PowerShell 5.1 (what a double-clicked .bat gets): no newer syntax.

.PARAMETER Action
    menu (default), desktop (VNC straight away), setup, undo, diagnose, selftest, dryrun
#>
[CmdletBinding()]
param(
    [string]$Action = "menu",
    [string]$Task = "",            # elevated child: ics-enable, ics-disable, static-enable, static-disable
    [string]$Public = "",
    [string]$Private = "",
    [string]$Result = ""
)

$ErrorActionPreference = "Stop"
$Repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$StateDir = Join-Path $env:LOCALAPPDATA "StepperMotorController"
$StateFile = Join-Path $StateDir "robot_link.json"
$SshDir = Join-Path $env:USERPROFILE ".ssh"
$KeyFile = Join-Path $SshDir "id_ed25519"
$SshConfig = Join-Path $SshDir "config"
$HostAlias = "robot"
$StaticPc = "192.168.50.1"
$StaticPi = "192.168.50.2"
# Raspberry Pi Foundation / Raspberry Pi Trading MAC prefixes (OUI)
$PiOuis = @("B8-27-EB", "DC-A6-32", "E4-5F-01", "28-CD-C1", "D8-3A-DD", "2C-CF-67", "88-A2-9E")

# ------------------------------------------------------------------------------------------------
# Small helpers
# ------------------------------------------------------------------------------------------------
function Say([string]$text, [string]$colour = "Gray") { Write-Host $text -ForegroundColor $colour }
function Ok([string]$text)   { Say ("  OK    " + $text) "Green" }
function Warn([string]$text) { Say ("  WARN  " + $text) "Yellow" }
function Bad([string]$text)  { Say ("  FAIL  " + $text) "Red" }
function Head([string]$text) { Say ""; Say ("== " + $text) "Cyan" }

function Ask([string]$question, [bool]$default = $true) {
    $hint = "[y/N]"
    if ($default) { $hint = "[Y/n]" }
    while ($true) {
        $a = (Read-Host ("  " + $question + " " + $hint)).Trim().ToLower()
        if ($a -eq "") { return $default }
        if ($a -in @("y", "yes")) { return $true }
        if ($a -in @("n", "no")) { return $false }
    }
}

function Load-State {
    if (Test-Path $StateFile) {
        try { return (Get-Content $StateFile -Raw | ConvertFrom-Json) } catch { }
    }
    return [pscustomobject]@{}
}

function Save-State($state) {
    if (-not (Test-Path $StateDir)) { New-Item -ItemType Directory -Path $StateDir | Out-Null }
    $state | ConvertTo-Json | Set-Content -Path $StateFile -Encoding ASCII
}

function Set-StateValue($state, [string]$name, $value) {
    if ($state.PSObject.Properties[$name]) { $state.$name = $value }
    else { $state | Add-Member -NotePropertyName $name -NotePropertyValue $value }
}

function Get-StateValue($state, [string]$name, $default = $null) {
    if ($state.PSObject.Properties[$name]) { return $state.$name }
    return $default
}

function Is-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    return (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

# ------------------------------------------------------------------------------------------------
# Pure parts (the self-test checks these)
# ------------------------------------------------------------------------------------------------
function Merge-SshConfig([string]$existing, [string]$hostName, [string]$user) {
    # The "Host robot" block between our markers, replaced on every run (the address can change)
    $begin = "# >>> robot (scripts/pi/robot_link.ps1: managed, rewritten on every setup)"
    $end = "# <<< robot"
    $block = @(
        $begin,
        "Host $HostAlias",
        "    HostName $hostName",
        "    User $user",
        "    IdentityFile ~/.ssh/id_ed25519",
        "    ServerAliveInterval 15",
        "    ServerAliveCountMax 4",
        $end
    ) -join "`n"
    if ($null -eq $existing) { $existing = "" }
    $text = $existing -replace "`r`n", "`n"
    $pattern = "(?s)" + [regex]::Escape($begin) + ".*?" + [regex]::Escape($end)
    if ($text -match $pattern) {
        $text = [regex]::Replace($text, $pattern, [System.Text.RegularExpressions.MatchEvaluator]{ param($m) $block })
    } else {
        if ($text.Length -gt 0 -and -not $text.EndsWith("`n")) { $text += "`n" }
        if ($text.Length -gt 0) { $text += "`n" }
        $text += $block + "`n"
    }
    return $text
}

function Test-PiMac([string]$mac) {
    if (-not $mac) { return $false }
    $m = $mac.ToUpper().Replace(":", "-")
    foreach ($oui in $PiOuis) { if ($m.StartsWith($oui)) { return $true } }
    return $false
}

function Get-Prefix24([string]$ip) {
    $p = $ip.Split(".")
    return ($p[0] + "." + $p[1] + "." + $p[2] + ".")
}

# ------------------------------------------------------------------------------------------------
# 1. The SSH key and config
# ------------------------------------------------------------------------------------------------
function Ensure-SshKey([switch]$DryRun) {
    Head "SSH key"
    if (-not (Get-Command ssh.exe -ErrorAction SilentlyContinue)) {
        Bad "ssh.exe is missing: Settings > System > Optional features > add 'OpenSSH Client'"
        return $false
    }
    if (-not (Test-Path $SshDir)) { New-Item -ItemType Directory -Path $SshDir | Out-Null }
    if (Test-Path $KeyFile) {
        Ok "$KeyFile exists"
    } elseif ($DryRun) {
        Say ("  would make " + $KeyFile + " (dry run)") "DarkGray"
        return $true
    } else {
        & ssh-keygen.exe -q -t ed25519 -N '""' -f $KeyFile -C ("robot-link@" + $env:COMPUTERNAME) | Out-Null
        if (-not (Test-Path $KeyFile)) {
            # older ssh-keygen builds want an empty argument, not two quotes
            & ssh-keygen.exe -q -t ed25519 -N "" -f $KeyFile -C ("robot-link@" + $env:COMPUTERNAME) | Out-Null
        }
        Ok "made $KeyFile"
    }
    $pub = (Get-Content ($KeyFile + ".pub") -Raw).Trim()
    Say "  This PC's public key (paste it into Raspberry Pi Imager > Services > 'Allow public-key"
    Say "  authentication only' when you image the card; it is on the clipboard now):"
    Say ("  " + $pub) "White"
    try { Set-Clipboard -Value $pub } catch { }
    return $true
}

function Ensure-SshConfig([string]$hostName, [string]$user) {
    $existing = ""
    if (Test-Path $SshConfig) { $existing = Get-Content $SshConfig -Raw }
    $merged = Merge-SshConfig $existing $hostName $user
    if ($merged -ne ($existing -replace "`r`n", "`n")) {
        Set-Content -Path $SshConfig -Value $merged -Encoding ASCII -NoNewline
        Ok "~/.ssh/config: Host robot -> $user@$hostName"
    } else {
        Ok "~/.ssh/config: Host robot already -> $user@$hostName"
    }
}

# ------------------------------------------------------------------------------------------------
# 2. The network link
# ------------------------------------------------------------------------------------------------
function Get-WiredAdapter($state) {
    $wired = @(Get-NetAdapter -Physical -ErrorAction SilentlyContinue | Where-Object {
        $_.MediaType -eq "802.3" -and $_.InterfaceDescription -notmatch "Virtual|Hyper-V|VPN|Bluetooth|TAP|Loopback"
    })
    if ($wired.Count -eq 0) {
        Bad "no wired Ethernet adapter on this PC (a USB-Ethernet dongle works)"
        return $null
    }
    $up = @($wired | Where-Object { $_.Status -eq "Up" })
    if ($up.Count -eq 0) {
        Bad ("no link on " + (($wired | ForEach-Object { $_.Name }) -join ", ") +
             ": the cable is not in, or the Pi is off (no link light on the PC's port)")
        return $null
    }
    if ($up.Count -eq 1) { return $up[0] }
    $saved = Get-StateValue $state "ethernet"
    $pick = $up | Where-Object { $_.Name -eq $saved }
    if ($pick) { return $pick }
    Say "  More than one wired adapter has a link:"
    for ($i = 0; $i -lt $up.Count; $i++) { Say ("   [" + ($i + 1) + "] " + $up[$i].Name + "  (" + $up[$i].InterfaceDescription + ")") }
    while ($true) {
        $n = Read-Host "  Which one goes to the Pi?"
        if ($n -match '^\d+$' -and [int]$n -ge 1 -and [int]$n -le $up.Count) { return $up[[int]$n - 1] }
    }
}

function Get-InternetAdapter([string]$exclude) {
    $routes = Get-NetRoute -DestinationPrefix "0.0.0.0/0" -ErrorAction SilentlyContinue |
        Sort-Object { $_.RouteMetric + $_.InterfaceMetric }
    foreach ($r in $routes) {
        $a = Get-NetAdapter -InterfaceIndex $r.ifIndex -ErrorAction SilentlyContinue
        if ($a -and $a.Name -ne $exclude -and $a.Status -eq "Up") {
            $profile = Get-NetConnectionProfile -InterfaceIndex $r.ifIndex -ErrorAction SilentlyContinue
            if ($profile -and $profile.IPv4Connectivity -eq "Internet") { return $a }
        }
    }
    return $null
}

function Get-IcsState {
    # Which connections share now: (name, "public" | "private")
    $out = @()
    try {
        $m = New-Object -ComObject HNetCfg.HNetShare
        foreach ($c in $m.EnumEveryConnection) {
            $props = $m.NetConnectionProps.Invoke($c)
            $cfg = $m.INetSharingConfigurationForINetConnection.Invoke($c)
            if ($cfg.SharingEnabled) {
                $kind = "private"
                if ($cfg.SharingConnectionType -eq 0) { $kind = "public" }
                $out += [pscustomobject]@{ Name = $props.Name; Kind = $kind }
            }
        }
    } catch { }
    return $out
}

function Invoke-Elevated([string]$task, [string]$public = "", [string]$private = "") {
    $result = Join-Path $env:TEMP ("robot_link_" + [guid]::NewGuid().ToString("N") + ".txt")
    $args = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$PSCommandPath`"",
              "-Task", $task, "-Public", "`"$public`"", "-Private", "`"$private`"", "-Result", "`"$result`"")
    if (Is-Admin) {
        & powershell.exe @args | Out-Null
    } else {
        Say "  (Windows asks for administrator rights for this step)"
        try { Start-Process powershell.exe -Verb RunAs -Wait -ArgumentList $args } catch { Bad "administrator rights refused"; return $false }
    }
    if (Test-Path $result) {
        $text = (Get-Content $result -Raw).Trim()
        Remove-Item $result -ErrorAction SilentlyContinue
        if ($text -eq "ok") { return $true }
        Bad $text
        return $false
    }
    Bad "the elevated step did not report back"
    return $false
}

function Run-ElevatedTask {
    # Runs as administrator: one network change, its outcome written to $Result
    $msg = "ok"
    try {
        switch ($Task) {
            "ics-enable" {
                $m = New-Object -ComObject HNetCfg.HNetShare
                $conns = @($m.EnumEveryConnection)
                foreach ($c in $conns) {
                    $cfg = $m.INetSharingConfigurationForINetConnection.Invoke($c)
                    if ($cfg.SharingEnabled) { $cfg.DisableSharing() }
                }
                $donePub = $false; $donePriv = $false
                foreach ($c in $conns) {
                    $props = $m.NetConnectionProps.Invoke($c)
                    $cfg = $m.INetSharingConfigurationForINetConnection.Invoke($c)
                    if ($props.Name -eq $Public) { $cfg.EnableSharing(0); $donePub = $true }
                    elseif ($props.Name -eq $Private) { $cfg.EnableSharing(1); $donePriv = $true }
                }
                if (-not ($donePub -and $donePriv)) { $msg = "ICS: '$Public' or '$Private' not found among the connections" }
            }
            "ics-disable" {
                $m = New-Object -ComObject HNetCfg.HNetShare
                foreach ($c in $m.EnumEveryConnection) {
                    $props = $m.NetConnectionProps.Invoke($c)
                    $cfg = $m.INetSharingConfigurationForINetConnection.Invoke($c)
                    if ($cfg.SharingEnabled -and ($props.Name -eq $Public -or $props.Name -eq $Private)) { $cfg.DisableSharing() }
                }
            }
            "static-enable" {
                $have = Get-NetIPAddress -InterfaceAlias $Private -AddressFamily IPv4 -ErrorAction SilentlyContinue |
                    Where-Object { $_.IPAddress -eq $StaticPc }
                if (-not $have) {
                    New-NetIPAddress -InterfaceAlias $Private -IPAddress $StaticPc -PrefixLength 24 | Out-Null
                }
            }
            "static-disable" {
                Remove-NetIPAddress -InterfaceAlias $Private -IPAddress $StaticPc -Confirm:$false -ErrorAction SilentlyContinue
                Set-NetIPInterface -InterfaceAlias $Private -AddressFamily IPv4 -Dhcp Enabled -ErrorAction SilentlyContinue
            }
            default { $msg = "unknown task $Task" }
        }
    } catch {
        $msg = "$Task failed: " + $_.Exception.Message
    }
    Set-Content -Path $Result -Value $msg -Encoding ASCII
}

function Ensure-Network($state, [switch]$DryRun) {
    Head "Network link"
    $eth = Get-WiredAdapter $state
    if (-not $eth) { return $null }
    Ok ("wired adapter: " + $eth.Name + " (" + $eth.InterfaceDescription + ")")
    Set-StateValue $state "ethernet" $eth.Name
    $inet = Get-InternetAdapter $eth.Name
    $mode = Get-StateValue $state "mode"
    $ics = Get-IcsState
    if ($inet) {
        Ok ("internet on: " + $inet.Name + " -> share it to " + $eth.Name + " (ICS)")
        $sharing = ($ics | Where-Object { $_.Name -eq $inet.Name -and $_.Kind -eq "public" }) -and
                   ($ics | Where-Object { $_.Name -eq $eth.Name -and $_.Kind -eq "private" })
        if ($sharing) {
            Ok "Internet Connection Sharing already on"
        } else {
            $others = @($ics | Where-Object { $_.Name -ne $inet.Name -and $_.Name -ne $eth.Name })
            Say ("  This turns on Windows Internet Connection Sharing from '" + $inet.Name + "' to '" + $eth.Name + "'.")
            Say ("  Windows then gives '" + $eth.Name + "' the address 192.168.137.1 and hands the Pi an address")
            Say "  (DHCP) and internet through the cable. Undo: Robot.bat > [5] > undo."
            if ($others.Count -gt 0) {
                Warn ("sharing is on for " + (($others | ForEach-Object { $_.Name }) -join ", ") +
                      " (Mobile Hotspot?): ICS shares one connection at a time, that one stops")
            }
            if ($DryRun) { Say "  (dry run: not changed)" "DarkGray"; return $eth }
            if (-not (Ask "Turn on Internet Connection Sharing?")) { Warn "not changed"; return $eth }
            if (Invoke-Elevated "ics-enable" $inet.Name $eth.Name) {
                Ok "Internet Connection Sharing on"
                Start-Sleep -Seconds 3
            } else { return $null }
        }
        Set-StateValue $state "mode" "ics"
        Set-StateValue $state "icsPublic" $inet.Name
    } else {
        $have = Get-NetIPAddress -InterfaceAlias $eth.Name -AddressFamily IPv4 -ErrorAction SilentlyContinue |
            Where-Object { $_.IPAddress -eq $StaticPc }
        if ($have) {
            Ok ("fixed address " + $StaticPc + "/24 on " + $eth.Name)
        } else {
            Say "  No connection on this PC has internet, so the link uses fixed private addresses:"
            Say ("  PC " + $StaticPc + "/24 on '" + $eth.Name + "', Pi " + $StaticPi + "/24 (set on the Pi by pi_setup.sh).")
            Say "  Undo: Robot.bat > [5] > undo."
            if ($DryRun) { Say "  (dry run: not changed)" "DarkGray"; return $eth }
            if (-not (Ask ("Give '" + $eth.Name + "' the address " + $StaticPc + "?"))) { Warn "not changed"; return $eth }
            if (-not (Invoke-Elevated "static-enable" "" $eth.Name)) { return $null }
            Ok ("fixed address " + $StaticPc + "/24 on " + $eth.Name)
        }
        Set-StateValue $state "mode" "static"
    }
    if (-not $DryRun) { Save-State $state }
    return $eth
}

function Undo-Network($state) {
    Head "Undo the network changes"
    $eth = Get-StateValue $state "ethernet"
    $mode = Get-StateValue $state "mode"
    if (-not $eth -or -not $mode) { Ok "nothing recorded to undo"; return }
    if ($mode -eq "ics") {
        $pub = Get-StateValue $state "icsPublic" ""
        if (Ask ("Turn off Internet Connection Sharing (" + $pub + " -> " + $eth + ")?")) {
            if (Invoke-Elevated "ics-disable" $pub $eth) { Ok "sharing off" }
        }
    } else {
        if (Ask ("Remove " + $StaticPc + " from '" + $eth + "' and go back to DHCP?")) {
            if (Invoke-Elevated "static-disable" "" $eth) { Ok "back to DHCP" }
        }
    }
    Set-StateValue $state "mode" $null
    Save-State $state
    Say "  On the Pi (static mode only): sudo nmcli connection delete robot-direct"
}

# ------------------------------------------------------------------------------------------------
# 3. Finding the Pi
# ------------------------------------------------------------------------------------------------
function Test-Port22([string]$address, [int]$timeoutMs = 1500) {
    $c = New-Object System.Net.Sockets.TcpClient
    try {
        $iar = $c.BeginConnect($address, 22, $null, $null)
        if ($iar.AsyncWaitHandle.WaitOne($timeoutMs) -and $c.Connected) { return $true }
        return $false
    } catch { return $false } finally { $c.Close() }
}

function Resolve-Mdns([string]$name) {
    try {
        $t = [System.Net.Dns]::GetHostAddressesAsync($name)
        if ($t.Wait(3000)) {
            $v4 = $t.Result | Where-Object { $_.AddressFamily -eq "InterNetwork" } | Select-Object -First 1
            if ($v4) { return $v4.IPAddressToString }
        }
    } catch { }
    return $null
}

function Find-Pi($eth, $state) {
    Head "Finding the Pi"
    $tried = @()
    $last = Get-StateValue $state "piAddress"
    if ($last) {
        $tried += $last
        if (Test-Port22 $last) { Ok ("the Pi answers at " + $last + " (last time's address)"); return $last }
    }
    $ip = Resolve-Mdns "robot.local"
    if ($ip) {
        if (Test-Port22 $ip) { Ok ("robot.local -> " + $ip + ", SSH answers"); return $ip }
        Warn ("robot.local -> " + $ip + " but SSH does not answer (SSH not enabled in Imager? still booting?)")
    } else {
        Say "  robot.local does not resolve (mDNS); scanning the cable's subnet"
    }
    if (-not $eth) { return $null }
    $mine = Get-NetIPAddress -InterfaceIndex $eth.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue |
        Where-Object { $_.IPAddress -notlike "169.254.*" } | Select-Object -First 1
    if (-not $mine) {
        Warn ("'" + $eth.Name + "' has no address yet (only a link-local one): run the network step ([5])")
        return $null
    }
    $prefix = Get-Prefix24 $mine.IPAddress
    $pings = @()
    for ($i = 1; $i -le 254; $i++) {
        $addr = $prefix + $i
        if ($addr -eq $mine.IPAddress) { continue }
        $p = New-Object System.Net.NetworkInformation.Ping
        $pings += [pscustomobject]@{ Address = $addr; Task = $p.SendPingAsync($addr, 600) }
    }
    try { [System.Threading.Tasks.Task]::WaitAll(@($pings | ForEach-Object { $_.Task }), 4000) | Out-Null } catch { }
    $alive = @($pings | Where-Object { $_.Task.IsCompleted -and -not $_.Task.IsFaulted -and $_.Task.Result.Status -eq "Success" } |
               ForEach-Object { $_.Address })
    $neigh = @(Get-NetNeighbor -InterfaceIndex $eth.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue |
               Where-Object { $_.State -ne "Unreachable" -and $_.IPAddress -like ($prefix + "*") })
    $pis = @($neigh | Where-Object { Test-PiMac $_.LinkLayerAddress } | ForEach-Object { $_.IPAddress })
    $candidates = @($pis) + @($alive | Where-Object { $pis -notcontains $_ })
    foreach ($a in $candidates) {
        if (Test-Port22 $a) {
            $how = "answers on SSH"
            if ($pis -contains $a) { $how = "Raspberry Pi MAC, answers on SSH" }
            Ok ("found " + $a + " (" + $how + ")")
            return $a
        }
    }
    if ($pis.Count -gt 0) {
        Bad ("a Raspberry Pi is on the cable at " + ($pis -join ", ") + " but SSH does not answer: was SSH enabled in Imager? (first boot can take 2-3 minutes)")
    } elseif ($alive.Count -gt 0) {
        Bad ("devices answer on " + $prefix + "x (" + ($alive -join ", ") + ") but none is a Pi with SSH")
    } else {
        Bad ("nothing answers on " + $prefix + "x: the Pi is still booting (first boot 2-3 min), or it has no address yet")
    }
    return $null
}

# ------------------------------------------------------------------------------------------------
# 4. The key on the Pi, the repository, pi_setup.sh
# ------------------------------------------------------------------------------------------------
function Invoke-Ssh([string[]]$sshArgs) {
    $prev = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $out = & ssh.exe @sshArgs 2>&1 | Out-String
    $code = $LASTEXITCODE
    $ErrorActionPreference = $prev
    return [pscustomobject]@{ Code = $code; Text = $out }
}

function Ensure-PiAccess([string]$address, [string]$user) {
    Head "SSH to the Pi"
    $r = Invoke-Ssh @("-o", "BatchMode=yes", "-o", "ConnectTimeout=8", "-o", "StrictHostKeyChecking=accept-new", $HostAlias, "true")
    if ($r.Code -eq 0) { Ok ("ssh " + $HostAlias + " works (key accepted)"); return $true }
    if ($r.Text -match "REMOTE HOST IDENTIFICATION HAS CHANGED|Host key verification failed") {
        Warn "the Pi's host key changed (normal after re-imaging the card)"
        if (Ask "Forget the old key for the Pi?") {
            foreach ($h in @($address, "robot.local", $HostAlias)) { & ssh-keygen.exe -R $h 2>&1 | Out-Null }
            return (Ensure-PiAccess $address $user)
        }
        return $false
    }
    if ($r.Text -match "Permission denied") {
        Say "  The Pi does not know this PC's key yet: copying it (the Pi's password, once)."
        $pub = (Get-Content ($KeyFile + ".pub") -Raw).Trim()
        $remote = 'umask 077; mkdir -p ~/.ssh; touch ~/.ssh/authorized_keys; k="$(cat)"; grep -qxF "$k" ~/.ssh/authorized_keys || echo "$k" >> ~/.ssh/authorized_keys'
        $pub | & ssh.exe -o StrictHostKeyChecking=accept-new -o PubkeyAuthentication=no ($user + "@" + $address) $remote
        $r = Invoke-Ssh @("-o", "BatchMode=yes", "-o", "ConnectTimeout=8", $HostAlias, "true")
        if ($r.Code -eq 0) { Ok "key copied; ssh robot works"; return $true }
        Bad "still refused. If Imager set 'public-key only', paste this PC's key there (shown above) and re-image."
        return $false
    }
    Bad ("ssh failed: " + $r.Text.Trim())
    return $false
}

function Invoke-PiSetup($state) {
    Head "Setting up the Pi (pi_setup.sh)"
    $dirty = (& git -C $Repo status --porcelain 2>$null | Out-String).Trim()
    if ($dirty) { Warn "uncommitted changes on this PC are not sent (the Pi gets the commits)" }
    $bundle = Join-Path $env:TEMP "smc.bundle"
    & git -C $Repo bundle create $bundle --all 2>&1 | Out-Null
    if (-not (Test-Path $bundle)) { Bad "git bundle failed (is git installed?)"; return $false }
    $mode = Get-StateValue $state "mode" "ics"
    & scp.exe -q $bundle ($HostAlias + ":/tmp/smc.bundle")
    # LF only: a Windows checkout may have given the script CRLF, which bash on the Pi refuses
    $script = Join-Path $env:TEMP "smc_pi_setup.sh"
    $text = [IO.File]::ReadAllText((Join-Path $PSScriptRoot "pi_setup.sh")) -replace "`r`n", "`n"
    [IO.File]::WriteAllText($script, $text, (New-Object System.Text.UTF8Encoding($false)))
    & scp.exe -q $script ($HostAlias + ":/tmp/smc_pi_setup.sh")
    if ($LASTEXITCODE -ne 0) { Bad "copying to the Pi failed"; return $false }
    Ok "repository bundle and pi_setup.sh copied"
    & ssh.exe -t $HostAlias ("bash /tmp/smc_pi_setup.sh --bundle /tmp/smc.bundle --link " + $mode)
    if ($LASTEXITCODE -ne 0) { Bad "pi_setup.sh reported a problem (see above)"; return $false }
    Set-StateValue $state "setupDone" (Get-Date -Format s)
    Save-State $state
    return $true
}

function Run-Setup([switch]$DryRun) {
    $state = Load-State
    if (-not (Ensure-SshKey -DryRun:$DryRun)) { return $false }
    $user = Get-StateValue $state "user"
    if (-not $user -and -not $DryRun) {
        $user = (Read-Host "  The Pi's username (the one you set in Imager)").Trim()
        Set-StateValue $state "user" $user
    }
    $eth = Ensure-Network $state -DryRun:$DryRun
    if ($DryRun) { Say ""; Say "Dry run: nothing changed." "DarkGray"; return $true }
    $ip = Find-Pi $eth $state
    if (-not $ip) { Save-State $state; return $false }
    Set-StateValue $state "piAddress" $ip
    Save-State $state
    Head "SSH config"
    Ensure-SshConfig $ip $user
    if (-not (Ensure-PiAccess $ip $user)) { return $false }
    return (Invoke-PiSetup $state)
}

# ------------------------------------------------------------------------------------------------
# Menu actions
# ------------------------------------------------------------------------------------------------
function Ensure-Ready {
    $state = Load-State
    if (-not (Get-StateValue $state "setupDone")) {
        Say "First run: the one-time setup." "Cyan"
        if (-not (Run-Setup)) { return $null }
        $state = Load-State
    }
    $ip = Get-StateValue $state "piAddress"
    if ($ip -and (Test-Port22 $ip)) { return $state }
    $eth = Get-WiredAdapter $state
    $ip = Find-Pi $eth $state
    if (-not $ip) { return $null }
    Set-StateValue $state "piAddress" $ip
    Save-State $state
    Ensure-SshConfig $ip (Get-StateValue $state "user")
    return $state
}

function Open-Terminal {
    if (-not (Ensure-Ready)) { return }
    if (Get-Command wt.exe -ErrorAction SilentlyContinue) { Start-Process wt.exe -ArgumentList @("ssh", $HostAlias) }
    else { Start-Process cmd.exe -ArgumentList @("/c", "start", "ssh", $HostAlias) }
}

function Find-VncViewer {
    $paths = @(
        "$env:ProgramFiles\RealVNC\VNC Viewer\vncviewer.exe",
        "${env:ProgramFiles(x86)}\RealVNC\VNC Viewer\vncviewer.exe",
        "$env:LOCALAPPDATA\Programs\RealVNC\VNC Viewer\vncviewer.exe",
        "$env:ProgramFiles\TigerVNC\vncviewer.exe",
        "$env:ProgramFiles\TigerVNC\vncviewer64.exe",
        "${env:ProgramFiles(x86)}\TigerVNC\vncviewer.exe"
    )
    foreach ($p in $paths) { if ($p -and (Test-Path $p)) { return $p } }
    $cmd = Get-Command vncviewer.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

function Open-Desktop {
    $state = Ensure-Ready
    if (-not $state) { return }
    $viewer = Find-VncViewer
    if (-not $viewer) {
        Warn "no VNC viewer on this PC"
        if (Ask "Install RealVNC Viewer with winget (it works with the Pi's wayvnc)?") {
            & winget.exe install --id RealVNC.VNCViewer -e --accept-source-agreements --accept-package-agreements
            $viewer = Find-VncViewer
        }
        if (-not $viewer) { Bad "no viewer (TigerVNC works too: winget install TigerVNC.TigerVNC)"; return }
    }
    $ip = Get-StateValue $state "piAddress"
    Ok ("opening the Pi's desktop (" + $ip + ") in " + (Split-Path $viewer -Leaf) + "; log in with the Pi's username and password")
    Start-Process $viewer -ArgumentList @($ip)
}

function Find-Code {
    $cmd = Get-Command code.cmd -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($p in @("$env:LOCALAPPDATA\Programs\Microsoft VS Code\bin\code.cmd", "$env:ProgramFiles\Microsoft VS Code\bin\code.cmd")) {
        if (Test-Path $p) { return $p }
    }
    return $null
}

function Open-VsCode {
    $state = Ensure-Ready
    if (-not $state) { return }
    $code = Find-Code
    if (-not $code) { Bad "VS Code is not installed (winget install Microsoft.VisualStudioCode)"; return }
    $ext = & $code --list-extensions 2>$null
    if ($ext -notcontains "ms-vscode-remote.remote-ssh") {
        if (Ask "VS Code needs the Remote - SSH extension. Install it?") { & $code --install-extension ms-vscode-remote.remote-ssh | Out-Null }
    }
    $user = Get-StateValue $state "user"
    & $code --remote ("ssh-remote+" + $HostAlias) ("/home/" + $user + "/StepperMotorController")
}

function Open-RemoteGui {
    Warn "The robot GUI over the cable needs the board bridge on the Pi: it comes in the next step."
    Say "  For now: [2] opens the Pi's desktop, where the Robot GUI runs on the Pi itself."
}

function Run-Diagnose {
    $state = Load-State
    Head "Diagnostics"
    Say ("  state: " + $StateFile)
    Say ("  mode: " + (Get-StateValue $state "mode" "none") + ", Pi: " + (Get-StateValue $state "piAddress" "unknown") +
         ", user: " + (Get-StateValue $state "user" "unset") + ", setup: " + (Get-StateValue $state "setupDone" "never"))
    $eth = Get-WiredAdapter $state
    if ($eth) { Ok ("link up on " + $eth.Name) }
    $ics = Get-IcsState
    if ($ics.Count -gt 0) { Say ("  sharing: " + (($ics | ForEach-Object { $_.Name + " (" + $_.Kind + ")" }) -join ", ")) }
    $ip = Find-Pi $eth $state
    if ($ip) {
        $r = Invoke-Ssh @("-o", "BatchMode=yes", "-o", "ConnectTimeout=8", $HostAlias, "cat /etc/os-release | grep PRETTY; hostname -I; ls /dev/smc-* 2>/dev/null || echo 'no /dev/smc-* boards'")
        if ($r.Code -eq 0) { Say $r.Text } else { Warn ("ssh: " + $r.Text.Trim()) }
    }
}

function Show-Menu {
    while ($true) {
        $state = Load-State
        Say ""
        Say "  Robot link  (Pi: $(Get-StateValue $state 'piAddress' 'not found yet'), link: $(Get-StateValue $state 'mode' 'not set up'))" "Cyan"
        Say "  [1] Terminal on the Pi (SSH)"
        Say "  [2] Pi desktop (VNC)"
        Say "  [3] Robot GUI on this PC, boards on the Pi"
        Say "  [4] VS Code on the Pi (Remote-SSH)"
        Say "  [5] Re-run setup / diagnostics"
        Say "  [q] Quit"
        $c = (Read-Host "  Choose").Trim().ToLower()
        switch ($c) {
            "1" { Open-Terminal }
            "2" { Open-Desktop }
            "3" { Open-RemoteGui }
            "4" { Open-VsCode }
            "5" {
                Say "   [s] setup again   [d] diagnostics only   [u] undo the network changes"
                $s = (Read-Host "  Choose").Trim().ToLower()
                if ($s -eq "s") { Run-Setup | Out-Null }
                elseif ($s -eq "d") { Run-Diagnose }
                elseif ($s -eq "u") { Undo-Network (Load-State) }
            }
            "q" { return }
            default { }
        }
    }
}

function Run-SelfTest {
    $script:selfTestFail = 0
    function Check([string]$name, [bool]$ok) {
        if ($ok) { Ok $name } else { Bad $name; $script:selfTestFail++ }
    }
    $a = Merge-SshConfig "" "192.168.137.20" "pi"
    Check "config: a new block" ($a -match "Host robot" -and $a -match "HostName 192.168.137.20")
    $b = Merge-SshConfig ("Host other`n    HostName x`n`n" + $a) "192.168.137.33" "pi"
    Check "config: replaced, not added" (([regex]::Matches($b, "Host robot")).Count -eq 1 -and $b -match "192.168.137.33" -and $b -match "Host other")
    Check "config: idempotent" ((Merge-SshConfig $b "192.168.137.33" "pi") -eq $b)
    Check "MAC: a Pi 4" (Test-PiMac "dc:a6:32:12:34:56")
    Check "MAC: not a Pi" (-not (Test-PiMac "A0-AD-9F-89-CC-43"))
    Check "subnet prefix" ((Get-Prefix24 "192.168.137.1") -eq "192.168.137.")
    if ($script:selfTestFail -gt 0) { exit 1 }
}

# ------------------------------------------------------------------------------------------------
if ($Task) { Run-ElevatedTask; exit 0 }
switch ($Action.ToLower()) {
    "menu"     { Show-Menu }
    "desktop"  { Open-Desktop; Start-Sleep -Seconds 3 }
    "setup"    { Run-Setup | Out-Null }
    "undo"     { Undo-Network (Load-State) }
    "diagnose" { Run-Diagnose }
    "dryrun"   { Run-Setup -DryRun | Out-Null }
    "selftest" { Run-SelfTest }
    default    { Show-Menu }
}
