#!/usr/bin/env bash
# Set up the Raspberry Pi coordinator ("robot"). Run by the PC's launcher (scripts/pi/Robot.bat)
# over SSH, or by hand on the Pi:
#
#   bash pi_setup.sh [--bundle FILE] [--link ics|static] [--dry-run] [--no-upgrade]
#
#   --bundle FILE   the PC's repository as a git bundle (the launcher sends one): cloned, or the Pi's
#                   clone fast-forwarded to it. Without it the clone comes from GitHub.
#   --link MODE     how the PC reaches the Pi: "ics" (the PC shares its internet, the Pi takes DHCP:
#                   nothing to set) or "static" (no DHCP on the cable: 192.168.50.2/24 alongside DHCP)
#   --dry-run       say what would be done, change nothing (the CI test runs this)
#
# Idempotent: every step checks first, so running it twice is harmless. Steps that need the
# internet (apt, pip) are skipped with a note when the Pi has none.
set -u

REPO_URL="https://github.com/rcampiz/StepperMotorController.git"
REPO="${SMC_REPO:-$HOME/StepperMotorController}"
BUNDLE=""; LINK=""; DRY=0; UPGRADE=1
STATIC_ADDR="192.168.50.2/24"
BOARDS_CONF="$HOME/.config/smc/boards.conf"
UDEV_RULES="/etc/udev/rules.d/99-smc-boards.rules"
VNC_SIZE="1920x1080"

while [ $# -gt 0 ]; do
    case "$1" in
        --bundle) BUNDLE="$2"; shift ;;
        --link) LINK="$2"; shift ;;
        --dry-run) DRY=1 ;;
        --no-upgrade) UPGRADE=0 ;;
        -h|--help) sed -n '2,16p' "$0"; exit 0 ;;
        *) echo "unknown option $1"; exit 1 ;;
    esac
    shift
done

USER="${USER:-$(id -un)}"
SUMMARY=()
PROBLEMS=0
say()  { echo "==> $*"; }
ok()   { echo "    OK    $*"; }
warn() { echo "    WARN  $*"; }
note() { SUMMARY+=("$*"); }
run()  {   # run a command, or say it in a dry run
    if [ "$DRY" = 1 ]; then echo "    (dry run) $*"; return 0; fi
    "$@"
}
SUDO="sudo"
[ "$(id -u)" = 0 ] && SUDO=""
have() { command -v "$1" >/dev/null 2>&1; }
online() { getent hosts deb.debian.org >/dev/null 2>&1 && timeout 5 bash -c 'exec 3<>/dev/tcp/deb.debian.org/80' 2>/dev/null; }

# ---------------------------------------------------------------------------------------------
say "The system"
. /etc/os-release 2>/dev/null || true
ok "${PRETTY_NAME:-unknown OS} on $(uname -m)"
DESKTOP=0
if [ -e /usr/bin/labwc ] || [ -e /usr/bin/wayfire ] || [ -e /usr/bin/lxsession ] || dpkg -s raspberrypi-ui-mods >/dev/null 2>&1; then DESKTOP=1; fi
[ "$DESKTOP" = 1 ] && ok "desktop image" || ok "Lite image (no desktop: VNC and the GUI's icon are skipped)"

# ---------------------------------------------------------------------------------------------
say "Packages"
PKGS="git python3-venv python3-pip build-essential"
if online; then
    if [ "$UPGRADE" = 1 ]; then
        run $SUDO apt-get update -q
        run $SUDO env DEBIAN_FRONTEND=noninteractive apt-get full-upgrade -y -q
    fi
    run $SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y -q $PKGS
    ok "$PKGS"
else
    warn "no internet: apt skipped (ICS mode gives the Pi the PC's internet; re-run setup then)"
    for p in git python3; do have "$p" || { warn "$p is missing and cannot be installed offline"; PROBLEMS=$((PROBLEMS+1)); }; done
fi

# ---------------------------------------------------------------------------------------------
say "The repository ($REPO)"
if [ -n "$BUNDLE" ] && [ -f "$BUNDLE" ]; then
    if [ -d "$REPO/.git" ]; then
        run git -C "$REPO" fetch -q "$BUNDLE" "+refs/heads/*:refs/remotes/pc/*"
        BR="$(git -C "$REPO" rev-parse --abbrev-ref HEAD 2>/dev/null || echo master)"
        if [ "$DRY" = 1 ] || git -C "$REPO" merge --ff-only -q "pc/$BR" 2>/dev/null; then
            ok "updated to the PC's $BR ($(git -C "$REPO" rev-parse --short HEAD 2>/dev/null))"
        else
            warn "the Pi's clone has its own commits or changes: not fast-forwarded (the PC's are in pc/$BR)"
        fi
    else
        run git clone -q "$BUNDLE" "$REPO"
        run git -C "$REPO" remote set-url origin "$REPO_URL"
        ok "cloned from the PC's bundle"
    fi
elif [ -d "$REPO/.git" ]; then
    if online; then run git -C "$REPO" pull -q --ff-only || warn "git pull failed"; fi
    ok "present"
else
    if online; then run git clone -q "$REPO_URL" "$REPO"; ok "cloned from GitHub"; else warn "no bundle and no internet: no repository"; PROBLEMS=$((PROBLEMS+1)); fi
fi
if [ "$DRY" = 0 ] && [ -d "$REPO/.git" ]; then
    run git -C "$REPO" submodule update --init --recursive -q 2>/dev/null || true
fi

# ---------------------------------------------------------------------------------------------
say "The client (the GUI and the CLI: scripts/setup/bringup.sh --skip-firmware)"
if [ -x "$REPO/scripts/setup/bringup.sh" ] || [ -f "$REPO/scripts/setup/bringup.sh" ]; then
    if online; then BRINGUP_OPTS="--skip-firmware --install"; else BRINGUP_OPTS="--skip-firmware"; fi
    if [ "$DRY" = 1 ]; then
        echo "    (dry run) bash $REPO/scripts/setup/bringup.sh $BRINGUP_OPTS"
    else
        bash "$REPO/scripts/setup/bringup.sh" $BRINGUP_OPTS
        rc=$?
        # 2 / 3: something to do by hand (listed above), handled below where it can be
        [ "$rc" = 0 ] && ok "client ready" || warn "bringup.sh exit $rc (see its list above)"
    fi
fi
if ! id -nG "$USER" | tr ' ' '\n' | grep -qx dialout; then
    run $SUDO usermod -aG dialout "$USER"
    ok "$USER added to dialout (the Nucleos' serial ports; takes effect at the next login)"
else
    ok "$USER is in dialout"
fi

# ---------------------------------------------------------------------------------------------
say "The direct link"
if [ "$LINK" = "static" ]; then
    # No DHCP on the cable, so the Pi needs a fixed address there: added to the cable's own
    # profile (netplan-eth0 on Trixie) beside DHCP, not a profile of its own. DHCP stays, so a
    # router still gives an address when the Pi is plugged into one (the fixed one sits beside
    # it on a subnet the router does not use), and the DHCP wait is unbounded, so a bare cable
    # does not time the connection (and the fixed address) out. Applied in place (device
    # reapply): the cable is not taken down, nor this setup's SSH session with it.
    if ! have nmcli; then
        warn "NetworkManager (nmcli) is not here: set $STATIC_ADDR on eth0 by hand"
        PROBLEMS=$((PROBLEMS+1))
    else
        CON="$(nmcli -g GENERAL.CONNECTION device show eth0 2>/dev/null)"
        [ -n "$CON" ] || CON="$(nmcli -t -f NAME,DEVICE connection show | sed -n 's/:eth0$//p' | head -1)"
        if [ -z "$CON" ]; then
            warn "no NetworkManager profile for eth0: set $STATIC_ADDR by hand"
            PROBLEMS=$((PROBLEMS+1))
        elif nmcli -g ipv4.addresses connection show "$CON" | tr ',' '\n' | sed 's/ //g' | grep -qx "$STATIC_ADDR"; then
            ok "$CON: DHCP + $STATIC_ADDR (already)"
        else
            run $SUDO nmcli connection modify "$CON" +ipv4.addresses "$STATIC_ADDR" ipv4.dhcp-timeout infinity ipv4.may-fail yes
            if [ "$DRY" = 1 ] || $SUDO nmcli device reapply eth0 2>/dev/null; then
                ok "$CON: DHCP + $STATIC_ADDR on eth0"
            else
                # Not applicable in place: reconnect the cable once this setup is done
                $SUDO systemd-run --on-active=3 --unit=smc-link-up nmcli connection up "$CON" >/dev/null 2>&1 ||
                    (sleep 3; $SUDO nmcli connection up "$CON") >/dev/null 2>&1 &
                ok "$CON: DHCP + $STATIC_ADDR, applied in a few seconds (the cable reconnects)"
            fi
            note "undo the fixed address: sudo nmcli connection modify $CON -ipv4.addresses $STATIC_ADDR ipv4.dhcp-timeout 0"
        fi
    fi
else
    ok "ICS (or none given): DHCP from the PC, nothing to set"
fi

# ---------------------------------------------------------------------------------------------
say "Boards: /dev/smc-<role> by ST-LINK serial"
# Every ST-LINK on USB with its serial (the NUCLEO's on-board ST-LINK/V2-1 is 0483:374b)
mapfile -t STLINKS < <(for d in /dev/ttyACM*; do
    [ -e "$d" ] || continue
    s="$(udevadm info -q property -n "$d" 2>/dev/null | sed -n 's/^ID_SERIAL_SHORT=//p')"
    v="$(udevadm info -q property -n "$d" 2>/dev/null | sed -n 's/^ID_VENDOR_ID=//p')"
    [ "$v" = "0483" ] && echo "$d $s"
done)
for l in "${STLINKS[@]+"${STLINKS[@]}"}"; do ok "ST-LINK ${l#* } on ${l%% *}"; done
[ ${#STLINKS[@]} = 0 ] && ok "no Nucleo plugged in now"
if [ ! -f "$BOARDS_CONF" ]; then
    run mkdir -p "$(dirname "$BOARDS_CONF")"
    if [ "$DRY" = 0 ]; then
        {
            echo "# Which Nucleo is which wheel: ROLE=ST-LINK serial (udevadm info -q property -n /dev/ttyACM0 | grep SERIAL_SHORT)."
            echo "# Fill in, then re-run setup (Robot.bat > [5]): pi_setup.sh writes $UDEV_RULES from it."
            for r in FL FR RL RR; do echo "#$r="; done
            for l in "${STLINKS[@]+"${STLINKS[@]}"}"; do echo "# seen now: ${l#* } on ${l%% *}"; done
        } > "$BOARDS_CONF"
    fi
    warn "fill in $BOARDS_CONF (which ST-LINK serial is which wheel), then re-run setup"
fi
if [ -f "$BOARDS_CONF" ]; then
    RULES=""
    while IFS='=' read -r role serial; do
        case "$role" in FL|FR|RL|RR) ;; *) continue ;; esac
        [ -n "$serial" ] || continue
        RULES+="SUBSYSTEM==\"tty\", ATTRS{idVendor}==\"0483\", ATTRS{serial}==\"$serial\", SYMLINK+=\"smc-$role\", GROUP=\"dialout\", MODE=\"0660\""$'\n'
    done < "$BOARDS_CONF"
    if [ -n "$RULES" ]; then
        if [ -f "$UDEV_RULES" ] && [ "$(cat "$UDEV_RULES")" = "${RULES%$'\n'}" ]; then
            ok "$UDEV_RULES up to date"
        elif [ "$DRY" = 1 ]; then
            echo "    (dry run) write $UDEV_RULES:"; printf '%s' "$RULES" | sed 's/^/      /'
        else
            printf '%s' "$RULES" | $SUDO tee "$UDEV_RULES" >/dev/null
            $SUDO udevadm control --reload-rules && $SUDO udevadm trigger --subsystem-match=tty
            ok "$UDEV_RULES written"
        fi
    fi
fi

# ---------------------------------------------------------------------------------------------
say "VNC (the Pi's desktop from the PC)"
if [ "$DESKTOP" = 1 ] && have raspi-config; then
    run $SUDO raspi-config nonint do_vnc 0
    # The size the desktop has with no monitor plugged in
    if grep -q "do_vnc_resolution" "$(command -v raspi-config)" 2>/dev/null; then
        run $SUDO raspi-config nonint do_vnc_resolution "$VNC_SIZE"
        ok "VNC on, $VNC_SIZE when no monitor is plugged in"
    else
        ok "VNC on (this raspi-config has no headless-resolution option)"
    fi
else
    ok "skipped (Lite image, or no raspi-config)"
fi

# ---------------------------------------------------------------------------------------------
say "The robot GUI on the Pi's desktop"
if [ "$DESKTOP" = 1 ] && [ -f "$REPO/client/start_gui.py" ]; then
    ENTRY="[Desktop Entry]
Type=Application
Name=Robot GUI
Comment=GUI for the STM32 stepper motor controller
Exec=python3 $REPO/client/start_gui.py
Path=$REPO/client
Terminal=false
Categories=Development;Engineering;
Icon=applications-engineering"
    for f in "$HOME/.local/share/applications/smc-gui.desktop" "$HOME/Desktop/robot-gui.desktop"; do
        if [ -f "$f" ] && [ "$(cat "$f")" = "$ENTRY" ]; then ok "$f"; continue; fi
        run mkdir -p "$(dirname "$f")"
        if [ "$DRY" = 0 ]; then printf '%s\n' "$ENTRY" > "$f"; chmod +x "$f"; fi
        ok "$f"
    done
else
    ok "skipped (Lite image, or no client)"
fi

# ---------------------------------------------------------------------------------------------
say "Summary"
echo "    OS:        ${PRETTY_NAME:-unknown}"
echo "    address:   $(hostname -I 2>/dev/null | tr -s ' ')"
for svc in ssh wayvnc vncserver-x11-serviced; do
    if systemctl is-active --quiet "$svc" 2>/dev/null; then echo "    service:   $svc running"; fi
done
BOARDS="$(ls /dev/smc-* 2>/dev/null | tr '\n' ' ')"
echo "    boards:    ${BOARDS:-none named yet} (${#STLINKS[@]} ST-LINK(s) on USB)"
for s in "${SUMMARY[@]+"${SUMMARY[@]}"}"; do echo "    $s"; done
[ "$PROBLEMS" = 0 ] && exit 0 || exit 1
