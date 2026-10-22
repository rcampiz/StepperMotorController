# The Raspberry Pi coordinator: imaging and one double-click to connect

The robot's coordinator is a Raspberry Pi 4 Model B ("robot") wired to the Windows PC with a
direct Ethernet cable. After imaging the card, one double-click on the PC sets everything up and
connects: `scripts/pi/Robot.bat` (the menu) or `scripts/pi/Pi Desktop.bat` (straight to the Pi's
desktop). Both work from a fresh clone and are idempotent: running them again changes only what
is missing.

| File | Runs on | What it does |
|---|---|---|
| `scripts/pi/Robot.bat` | PC, double-click | the menu below |
| `scripts/pi/Pi Desktop.bat` | PC, double-click | the Pi's desktop (VNC), the setup first if it never ran |
| `scripts/pi/robot_link.ps1` | PC (Windows PowerShell 5.1) | the launcher both .bat files start |
| `scripts/pi/pi_setup.sh` | the Pi (sent and run over SSH) | packages, repository, client, network profile, boards, VNC, GUI icon |

## 1. Imaging the card (by hand)

**Release.** Raspberry Pi OS (64-bit) based on **Debian 13 "Trixie"**, the current release in
Raspberry Pi Imager 2.x. The robot GUI needs Trixie: Qt for Python publishes Raspberry Pi builds
for glibc 2.39 and later, and Bookworm has 2.36 (the command line, `smc`, runs on both). The
tooling was written for Trixie and tested without hardware (`pi_setup.sh` in an arm64 Trixie
container in CI, `.github/workflows/pi-setup.yml`); the first real card is its first hardware run.

**Desktop or Lite.** Desktop if you want the Pi's desktop over VNC (and the robot GUI running on
the Pi itself); Lite otherwise. Default to **Raspberry Pi OS (64-bit)**, the desktop one, for now.

**A new Pi, in short:** write the card with Imager (below), leave it in the PC, `Robot.bat` >
**[6] Prepare a new SD card**, put the card in the Pi, cable and power, wait 2-3 minutes, then
`Robot.bat` again: it finds the Pi and runs the setup, which asks for the Pi's password once.

**Raspberry Pi Imager** (2.0 or later: older ones cannot customise Trixie):

1. Device: Raspberry Pi 4. OS: Raspberry Pi OS (64-bit). Storage: the SD card.
2. Customisation:
   - Hostname: any (the launcher learns it after the first setup; the PC's alias is `robot`
     whatever it is). The bench Pi is `CampizRobot` (section 6).
   - Username and password: yours (the launcher asks for the username on its first run)
   - Wi-Fi: optional (the cable is enough; Wi-Fi lets the Pi reach the internet on its own)
   - Locale and time zone: yours
   - **SSH: enabled.** "Allow public-key authentication only" with this PC's key
     (`%USERPROFILE%\.ssh\id_ed25519.pub`, `ssh-ed25519 AAAA... robot-link@<PC>`; [6] makes it
     if missing and puts it on the clipboard), or password authentication: [6] adds the key to
     the card either way.
3. Write. Leave the card in the PC.
4. **`Robot.bat` > [6] Prepare a new SD card.** It finds the card's `bootfs` drive and adds to
   the first boot (cloud-init `user-data`, between `# >>> smc` and `# <<< smc` markers; Imager's
   original kept as `user-data.orig`): this PC's SSH key for the card's user, SSH on, and the
   cable's fixed address `192.168.50.2/24` with no DHCP time-out (what `pi_setup.sh` sets, see
   section 2), so the Pi answers on the cable from its first boot. It asks before writing, and
   only writes that one file on the drive named `bootfs`. It also records the card's hostname
   and user, and marks the setup as not done, so the next double-click runs it for the new Pi.
5. Eject the card, put it in the Pi, connect the cable PC to Pi, power the Pi.

**Why Imager stays manual.** Trixie moved first-boot customisation to cloud-init, which only
Imager 2.0 writes, and Imager's command line for it is not documented well enough to trust
unattended (there are open reports of customisation not being applied). Writing a card is also
the one step where a wrong disk choice destroys data. So Imager writes the card and does the
customisation it knows; [6] only adds to the file Imager wrote (checked with `cloud-init schema`
on the bench Pi, 2026-10-22).

## 2. The first double-click

Double-click `scripts/pi/Robot.bat` (or `Pi Desktop.bat`). The first run does the one-time setup,
then the menu shows:

```
[1] Terminal on the Pi (SSH)
[2] Pi desktop (VNC)
[3] Robot GUI on this PC, boards on the Pi
[4] VS Code on the Pi (Remote-SSH)
[5] Re-run setup / diagnostics
[6] Prepare a new SD card (after Raspberry Pi Imager)
```

**The setup, step by step** (also from [5] > s):

1. **SSH key.** `~/.ssh/id_ed25519` if missing (printed, on the clipboard), and a `Host robot`
   block in `~/.ssh/config` between `# >>> robot` and `# <<< robot` markers, rewritten with the
   Pi's address on every setup (nothing else in the file is touched).
2. **The network link,** on the wired adapter with a link light (asked only if there are two).
   It asks before changing anything, and needs administrator rights (Windows asks, for this
   step only):
   - **Fixed addresses** (the default, no question): PC `192.168.50.1/24` on the cable, Pi
     `192.168.50.2/24` (set on the Pi by `pi_setup.sh`). No Windows service involved, so it
     survives reboots and updates; the Pi gets its internet over Wi-Fi.
   - **Share the PC's internet** (`[5] > l` switches to it, and back): Windows Internet
     Connection Sharing from the adapter with internet to the cable, through the
     `HNetCfg.HNetShare` COM interface (the one the adapter's Properties > Sharing tab uses).
     Windows gives the cable `192.168.137.1` and hands the Pi an address by DHCP and internet
     through the cable. For a Pi with no Wi-Fi.
3. **Finding the Pi:** the last known address, `<hostname>.local` (mDNS: `robot.local` until
   the Pi's own name is known; it also finds the Pi over Wi-Fi), `192.168.50.2`, then a ping
   sweep of the cable's subnet and the ARP table for Raspberry Pi MAC prefixes (B8:27:EB,
   DC:A6:32, E4:5F:01, 28:CD:C1, D8:3A:DD, 2C:CF:67, 88:A2:9E), and before the cable has any
   IPv4, the Pi's IPv6 link-local address (every node on the cable asked, the Pi's MAC kept),
   each checked for SSH. After a fixed-address setup the `robot` alias moves to `192.168.50.2`. It says what it found and why
   not (no link light, a Pi without SSH, nothing answering yet).
4. **The key on the Pi** if it is not there (the Pi's password, once), then this PC's repository
   sent as a git bundle (the Pi gets exactly the PC's commits, internet or not; uncommitted
   changes are not sent) and `pi_setup.sh` run on the Pi.

**`pi_setup.sh` on the Pi** (idempotent; `--dry-run` says what it would do):

- **The Pi's password, once, first** (for sudo: Imager 2.x gives the user no passwordless sudo).
  Nothing else in the run asks for it.
- `apt` update and full-upgrade, git, python3-venv, pip, build tools (skipped with a note when the
  Pi has no internet).
- The repository at `~/StepperMotorController`: cloned from the bundle, or fast-forwarded to it
  (left alone, with a warning, if the Pi's clone has its own commits); `origin` points at GitHub.
- `scripts/setup/bringup.sh --skip-firmware --install`: the client's venv with the GUI.
- `dialout` group membership (the Nucleos' serial ports).
- **Fixed-address mode only:** `192.168.50.2/24` added to the cable's own NetworkManager profile
  (`netplan-eth0` on Trixie; NetworkManager writes it back to netplan) beside DHCP, with an
  unbounded DHCP wait. Why this and not a static profile: DHCP stays, so the Pi still gets an
  address when it is later plugged into a router (the fixed one sits beside it, harmlessly, on a
  subnet the router does not use); and on the bare cable, where no DHCP ever answers, the
  unbounded wait keeps the connection up. With the default 45 s wait the bench Pi's cable
  connection failed and dropped every address, IPv6 included (2026-10-22). Applied in place
  (`nmcli device reapply`), so the SSH session running the setup is not cut. Undo: `sudo nmcli
  connection modify netplan-eth0 -ipv4.addresses 192.168.50.2/24 ipv4.dhcp-timeout 0`.
- **Boards:** udev rules naming each Nucleo by its ST-LINK serial, `/dev/smc-FL`, `/dev/smc-FR`,
  `/dev/smc-RL`, `/dev/smc-RR` (`/etc/udev/rules.d/99-smc-boards.rules`), from
  **`scripts/pi/boards.conf` in the repository** (`FL=<serial>` and so on), so every Pi names
  the boards on its first run. A Pi's own `~/.config/smc/boards.conf` overrides a role (for a
  board only that Pi has). The setup lists every ST-LINK it sees and warns about one with no
  wheel; set it in `scripts/pi/boards.conf`, commit, and re-run setup.
- **VNC** (desktop image): `raspi-config nonint do_vnc 0` (WayVNC on Trixie) and a 1920x1080
  desktop when no monitor is plugged in.
- **The robot GUI on the Pi:** a "Robot GUI" icon on the desktop and in the menu
  (`client/start_gui.py`, which sets up anything missing, asking first).
- A summary: OS, addresses, services running, boards seen.

**The menu:**

- **[1] Terminal on the Pi:** Windows Terminal with `ssh robot`.
- **[2] Pi desktop:** the VNC viewer on the Pi (TigerVNC or RealVNC Viewer, whichever is
  installed; offers `winget install TigerVNC.TigerVNC` if neither is, RealVNC as the fallback:
  its winget entry's download answered 404 on 2026-10-22). The first time, accept the Pi's
  certificate (wayvnc makes its own). Log in with the Pi's
  username and password. The robot GUI is on its desktop. `Pi Desktop.bat` does this directly.
- **[3] Robot GUI on this PC, boards on the Pi:** needs the board bridge (next step of this work).
- **[4] VS Code on the Pi:** `code --remote ssh-remote+robot /home/<user>/StepperMotorController`
  (offers the Remote - SSH extension if it is missing).
- **[5] Re-run setup / diagnostics:** `s` setup again, `d` diagnostics only (the link, sharing,
  the Pi's OS, addresses and boards), `u` undo the network changes, `l` switch the link between
  fixed addresses and sharing the PC's internet (undoes the current one, then runs the setup).
- **[6] Prepare a new SD card:** section 1, step 4.

The launcher remembers what it set up in `%LOCALAPPDATA%\StepperMotorController\robot_link.json`
(the adapter, the link mode, the Pi's address and username).

## 3. The board bridge

To come: the service on the Pi that exposes the Nucleos to the PC's GUI through the shared core,
with a liveness session that stops every board if the PC goes silent, and menu item [3].

## 4. Troubleshooting

- **No link light / "no link on Ethernet".** The cable is not in at one end, or the Pi is off.
  The PC needs a wired port (a USB-Ethernet dongle works).
- **`robot.local` does not resolve.** mDNS on Windows is patchy; the launcher scans the cable's
  subnet instead. If nothing answers, the Pi is still booting (the first boot takes 2-3 minutes
  while it resizes the card and applies the customisation).
- **"a Raspberry Pi is on the cable but SSH does not answer".** SSH was not enabled in Imager:
  re-image, or plug a keyboard and screen in and run `sudo raspi-config` > Interface > SSH.
- **"the Pi's host key changed".** Normal after re-imaging the card: the launcher offers to forget
  the old key (`ssh-keygen -R`).
- **Key refused after imaging with "public-key only".** The key pasted into Imager was not this
  PC's: paste the one the launcher prints and re-image.
- **ICS quirks.** Windows Internet Connection Sharing allows one shared connection at a time:
  turning it on for the cable turns it off elsewhere (Mobile Hotspot uses it too; the launcher
  warns). After some Windows updates or a reboot ICS can stop handing out addresses: undo and
  set it up again from [5], or restart the "Internet Connection Sharing (ICS)" service. ICS
  always uses 192.168.137.0/24 on the cable. A VPN on the PC can stop the Pi's traffic from
  reaching the internet.
- **USB power.** Four Nucleos take more than the Pi 4's USB ports supply (1.2 A for all four
  together): use a powered USB hub, or feed the Nucleos 5 V from the robot's supply.
- **The robot GUI on the Pi does not start.** It needs Trixie (see section 1); on a Bookworm card,
  `client/start_gui.py` says so. The command line works on either.

## 5. Undoing the network changes

- **On the PC:** `Robot.bat` > [5] > `u` turns Internet Connection Sharing off (ICS mode) or
  removes 192.168.50.1 and puts the adapter back on DHCP (fixed mode). By hand: the wired
  adapter's Properties > Sharing tab (ICS), or its IPv4 settings back to automatic.
- **On the Pi** (fixed mode only): `sudo nmcli connection modify netplan-eth0 -ipv4.addresses
  192.168.50.2/24 ipv4.dhcp-timeout 0`.
- The SSH key and the `Host robot` block in `~/.ssh/config` are harmless to keep; delete the block
  between the `# >>> robot` and `# <<< robot` markers to remove it.

## 6. This robot's Pi (the bench record)

What the bench Pi is and how its first setup went, so the next card (or the next PC) starts from
known facts. Passwords are not recorded here.

**The Pi.** Raspberry Pi 4 Model B, Raspberry Pi OS (64-bit) with desktop, Debian 13 trixie on
aarch64, cloud-init 25.2. Imaged 2026-10-22 with Raspberry Pi Imager 2.x.

| | |
|---|---|
| Hostname | `CampizRobot` (`CampizRobot.local` by mDNS; the PC's alias is still `robot`) |
| User | `rcampiz` |
| Imager customisation | SSH public-key only, with this PC's key (`robot-link@CAMPIZFAMILY`); Wi-Fi `Camper2400MHz` (country US); time zone America/New_York; keyboard us |
| sudo | asks for the password: Imager 2.x writes `sudo: null` for the user, so there is no passwordless rule (the old `010_pi-nopasswd` file is not there). One password covered the other SSH sessions too (Trixie's `/etc/sudoers.d/010_global-tty`). |
| Wi-Fi (`wlan0`) | `192.168.0.133/24` by DHCP from the home router (can change; the launcher finds it again by mDNS), MAC 88:a2:9e:13:8e:94 |
| Cable (`eth0`) | `192.168.50.2/24` beside DHCP (fixed-address mode), MAC 88:a2:9e:13:8e:92 |
| NetworkManager profiles | `netplan-eth0`, `netplan-wlan0-Camper2400MHz` (generated from Imager's `/boot/firmware/network-config`) |
| Boards | one NUCLEO-F401RE so far: ST-LINK serial `066CFF545150898367165515`, `/dev/ttyACM0` (in `scripts/pi/boards.conf`) |

**The PC.** Windows 11; the cable on `Ethernet` (Realtek PCIe, 1 Gbps) at `192.168.50.1/24`,
internet on `Wi-Fi`. Link mode: fixed addresses (the Pi has its own internet over Wi-Fi). State in
`%LOCALAPPDATA%\StepperMotorController\robot_link.json`.

**What the first setup ran into** (each fixed in the scripts, so a new host does not):

1. **The first card had SSH off.** The Pi answered ping on the cable but refused port 22.
   Re-imaged with SSH on, public-key only, the launcher's key pasted.
2. **The cable lost every address.** With nothing on the cable handing out DHCP,
   NetworkManager's default 45 s DHCP wait failed `netplan-eth0`, and the failed connection
   dropped its IPv6 link-local address too, so the PC could not find the Pi at all. The Pi was
   reached over Wi-Fi (`CampizRobot.local`) instead, and `pi_setup.sh` now adds the fixed address
   with an unbounded DHCP wait (section 2).
3. **The launcher stopped on a git warning.** Windows PowerShell 5.1 turned git's stderr ("could
   not open directory client/.pytest_cache/") into a terminating error. Fixed: native programs'
   stderr no longer ends the setup.
4. **The launcher hung at "SSH to the Pi".** ssh read the console's input. Fixed: the checks run
   `ssh -n`.
5. **sudo's password prompt went unnoticed** partway through the run (after the copy, before
   `apt`), so the setup sat waiting. Fixed: `pi_setup.sh` asks for it first, saying so, and
   keeps sudo fresh to the end, so it never asks again in the middle.
