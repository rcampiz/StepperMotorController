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

**The PC's key first.** Double-click `scripts/pi/Robot.bat` once before imaging: it makes this
PC's SSH key (`%USERPROFILE%\.ssh\id_ed25519`), prints the public key and puts it on the
clipboard. With no cable plugged in it stops there, saying there is no link: that is expected.

**Raspberry Pi Imager** (2.0 or later: older ones cannot customise Trixie):

1. Device: Raspberry Pi 4. OS: Raspberry Pi OS (64-bit). Storage: the SD card.
2. Customisation:
   - Hostname: `robot`
   - Username and password: yours (the launcher asks for the username on its first run)
   - Wi-Fi: optional (the cable is enough; Wi-Fi lets the Pi reach the internet on its own)
   - Locale and time zone: yours
   - **SSH: enabled, "Allow public-key authentication only"**, and paste the public key the
     launcher printed (`ssh-ed25519 AAAA... robot-link@<PC>`). With password authentication
     instead, the launcher copies the key on its first connection (the Pi's password, once).
3. Write, put the card in the Pi, connect the cable PC to Pi, power the Pi.

**Why there is no `image_sd.ps1`.** Trixie moved first-boot customisation to cloud-init, which
only Imager 2.0 writes, and Imager's command line for it is not documented well enough to trust
unattended (there are open reports of customisation not being applied). Writing a card is also
the one step where a wrong disk choice destroys data. Imaging stays manual; the GUI above takes a
minute.

## 2. The first double-click

Double-click `scripts/pi/Robot.bat` (or `Pi Desktop.bat`). The first run does the one-time setup,
then the menu shows:

```
[1] Terminal on the Pi (SSH)
[2] Pi desktop (VNC)
[3] Robot GUI on this PC, boards on the Pi
[4] VS Code on the Pi (Remote-SSH)
[5] Re-run setup / diagnostics
```

**The setup, step by step** (also from [5] > s):

1. **SSH key.** `~/.ssh/id_ed25519` if missing (printed, on the clipboard), and a `Host robot`
   block in `~/.ssh/config` between `# >>> robot` and `# <<< robot` markers, rewritten with the
   Pi's address on every setup (nothing else in the file is touched).
2. **The network link,** on the wired adapter with a link light (asked only if there are two).
   It asks before changing anything:
   - **The PC has internet on another adapter (Wi-Fi):** Windows Internet Connection Sharing
     from that adapter to the cable, through the `HNetCfg.HNetShare` COM interface (the one the
     adapter's Properties > Sharing tab uses). Windows gives the cable `192.168.137.1` and hands
     the Pi an address by DHCP and internet through the cable. Needs administrator rights
     (Windows asks, for this step only).
   - **No internet on the PC:** fixed private addresses, PC `192.168.50.1/24` on the cable, Pi
     `192.168.50.2/24` (set on the Pi by `pi_setup.sh`).
3. **Finding the Pi:** the last known address, `robot.local` (mDNS), then a ping sweep of the
   cable's subnet and the ARP table for Raspberry Pi MAC prefixes (B8:27:EB, DC:A6:32, E4:5F:01,
   28:CD:C1, D8:3A:DD, 2C:CF:67, 88:A2:9E), each checked for SSH. It says what it found and why
   not (no link light, a Pi without SSH, nothing answering yet).
4. **The key on the Pi** if it is not there (the Pi's password, once), then this PC's repository
   sent as a git bundle (the Pi gets exactly the PC's commits, internet or not; uncommitted
   changes are not sent) and `pi_setup.sh` run on the Pi.

**`pi_setup.sh` on the Pi** (idempotent; `--dry-run` says what it would do):

- `apt` update and full-upgrade, git, python3-venv, pip, build tools (skipped with a note when the
  Pi has no internet).
- The repository at `~/StepperMotorController`: cloned from the bundle, or fast-forwarded to it
  (left alone, with a warning, if the Pi's clone has its own commits); `origin` points at GitHub.
- `scripts/setup/bringup.sh --skip-firmware --install`: the client's venv with the GUI.
- `dialout` group membership (the Nucleos' serial ports).
- **Fixed-address mode only:** a NetworkManager profile `robot-direct` on eth0 with DHCP **and**
  `192.168.50.2/24` beside it, and an unbounded DHCP wait. Why this and not a static profile:
  the same profile keeps working when the Pi is later plugged into a router (DHCP gives it an
  address as usual, the fixed one stays beside it, harmlessly, on a subnet the router does not
  use), and on the bare cable, where no DHCP ever answers, the unbounded wait keeps the
  connection (and so the fixed address) up instead of timing out. Undo: `sudo nmcli connection
  delete robot-direct`.
- **Boards:** udev rules naming each Nucleo by its ST-LINK serial, `/dev/smc-FL`, `/dev/smc-FR`,
  `/dev/smc-RL`, `/dev/smc-RR` (`/etc/udev/rules.d/99-smc-boards.rules`), from
  `~/.config/smc/boards.conf`. The first run writes that file with the serials it sees; set
  `FL=<serial>` and so on, then re-run setup.
- **VNC** (desktop image): `raspi-config nonint do_vnc 0` (WayVNC on Trixie) and a 1920x1080
  desktop when no monitor is plugged in.
- **The robot GUI on the Pi:** a "Robot GUI" icon on the desktop and in the menu
  (`client/start_gui.py`, which sets up anything missing, asking first).
- A summary: OS, addresses, services running, boards seen.

**The menu:**

- **[1] Terminal on the Pi:** Windows Terminal with `ssh robot`.
- **[2] Pi desktop:** the VNC viewer on the Pi (RealVNC Viewer or TigerVNC, whichever is
  installed; offers `winget install RealVNC.VNCViewer` if neither is). Log in with the Pi's
  username and password. The robot GUI is on its desktop. `Pi Desktop.bat` does this directly.
- **[3] Robot GUI on this PC, boards on the Pi:** needs the board bridge (next step of this work).
- **[4] VS Code on the Pi:** `code --remote ssh-remote+robot /home/<user>/StepperMotorController`
  (offers the Remote - SSH extension if it is missing).
- **[5] Re-run setup / diagnostics:** `s` setup again, `d` diagnostics only (the link, sharing,
  the Pi's OS, addresses and boards), `u` undo the network changes.

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
- **On the Pi** (fixed mode only): `sudo nmcli connection delete robot-direct`.
- The SSH key and the `Host robot` block in `~/.ssh/config` are harmless to keep; delete the block
  between the `# >>> robot` and `# <<< robot` markers to remove it.
