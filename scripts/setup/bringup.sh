#!/usr/bin/env bash
# Fresh-clone bring-up on Debian / Ubuntu / Raspberry Pi OS (the Linux twin of bringup.ps1).
#
#   scripts/setup/bringup.sh              check, create the Python venvs, print what needs sudo
#   scripts/setup/bringup.sh --install    also run the apt install (asks for your sudo password)
#   scripts/setup/bringup.sh --check      report only, change nothing
#   options: --skip-firmware (client-only machine, e.g. a Pi that just runs the GUI/CLI)
#            --with-chassis-tool (tools/chassis_cost/.venv, about 640 MB)
#
# Anything that needs root (apt, the dialout group, udev rules) is QUEUED and printed with
# the exact command, so you decide when to run it. Safe to re-run.
set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
INSTALL=0; CHECK=0; SKIP_FW=0; CHASSIS=0
for a in "$@"; do
    case "$a" in
        --install) INSTALL=1 ;;
        --check) CHECK=1 ;;
        --skip-firmware) SKIP_FW=1 ;;
        --with-chassis-tool) CHASSIS=1 ;;
        *) echo "unknown option $a"; exit 1 ;;
    esac
done

MANUAL=()
ROWS=()
row()    { ROWS+=("$(printf '%-28s %-8s %s' "$1" "$2" "$3")"); }
queue()  { MANUAL+=("$1"$'\n'"     why:  $2"$'\n'"     run:  $3"); }
step()   { echo "==> $*"; }

# ---------------------------------------------------------------------------
# 1. apt packages: command to verify -> package
# ---------------------------------------------------------------------------
declare -a NEED_PKGS=()
check_tool() {   # exe package role
    if command -v "$1" >/dev/null 2>&1; then
        row "$1" OK "$(command -v "$1")"
    else
        row "$1" MISSING "$3 (apt: $2)"
        NEED_PKGS+=("$2")
    fi
}
check_tool git git "clone, submodules"
check_tool python3 python3 "scripts, client, tools"
if python3 -c 'import venv' >/dev/null 2>&1; then row "python3-venv" OK "venv module present"; else row "python3-venv" MISSING "apt: python3-venv"; NEED_PKGS+=(python3-venv); fi
if python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3, 11) else 1)' 2>/dev/null; then :; else row "python >= 3.11" MISSING "$(python3 --version 2>&1): the client needs 3.11+ (Raspberry Pi OS bookworm has 3.11)"; fi
if [ "$SKIP_FW" = 0 ]; then
    check_tool arm-none-eabi-gcc gcc-arm-none-eabi "firmware cross-compiler"
    check_tool cmake cmake "build generator"
    check_tool ninja ninja-build "build backend"
    check_tool g++ build-essential "host unit tests, virtual Nucleo"
    check_tool openocd openocd "flashing / debug via ST-LINK"
fi
if [ ${#NEED_PKGS[@]} -gt 0 ]; then
    APT="sudo apt-get update && sudo apt-get install -y ${NEED_PKGS[*]}"
    if [ "$INSTALL" = 1 ] && [ "$CHECK" = 0 ]; then
        step "$APT"
        bash -c "$APT" || echo "apt failed; see above"
    else
        queue "System packages (root)" "missing: ${NEED_PKGS[*]}" "$APT     (or re-run with --install)"
    fi
fi

# ---------------------------------------------------------------------------
# 2. FreeRTOS submodule
# ---------------------------------------------------------------------------
KERNEL="$REPO/server/external/X_middlewares/Third_Party/FreeRTOS-Kernel"
if [ ! -f "$KERNEL/tasks.c" ] && [ "$CHECK" = 0 ] && command -v git >/dev/null && [ -d "$REPO/.git" ]; then
    step "git submodule update --init --recursive"
    git -C "$REPO" submodule update --init --recursive
fi
if [ -f "$KERNEL/tasks.c" ]; then row "FreeRTOS submodule" OK "$KERNEL"; else row "FreeRTOS submodule" MISSING "git submodule update --init --recursive"; fi

# ---------------------------------------------------------------------------
# 3. Python virtual environments
# ---------------------------------------------------------------------------
ensure_venv() {   # dir requirements extra label
    local py="$1/.venv/bin/python"
    if [ ! -x "$py" ]; then
        if [ "$CHECK" = 1 ]; then row "$4" MISSING "re-run without --check to create"; return; fi
        if ! python3 -c 'import venv, ensurepip' >/dev/null 2>&1; then row "$4" MISSING "python3-venv is not installed yet (see the apt line)"; return; fi
        step "python3 -m venv $1/.venv"
        python3 -m venv "$1/.venv" || { row "$4" MISSING "venv creation failed"; return; }
    fi
    if [ "$CHECK" = 0 ]; then
        step "pip install -r $2 $3"
        if ! "$py" -m pip install --quiet --disable-pip-version-check -r "$2" $3; then
            row "$4" MISSING "pip install failed. On a Raspberry Pi PySide6 may have no wheel: install the distro Qt packages (apt search pyside6) and recreate the venv with --system-site-packages; the CLI and tests need no Qt"
            return
        fi
    fi
    row "$4" OK "$py"
}
ensure_venv "$REPO/client" "$REPO/client/requirements.txt" "pytest" "client/.venv"
if [ "$CHASSIS" = 1 ]; then
    ensure_venv "$REPO/tools/chassis_cost" "$REPO/tools/chassis_cost/requirements.txt" "" "tools/chassis_cost/.venv"
else
    row "tools/chassis_cost/.venv" SKIPPED "optional: --with-chassis-tool (about 640 MB)"
fi

# ---------------------------------------------------------------------------
# 4. Serial port access and ST-LINK udev rule (root)
# ---------------------------------------------------------------------------
if id -nG "$USER" | tr ' ' '\n' | grep -qx dialout; then
    row "dialout group" OK "$USER can open /dev/ttyACM*"
else
    row "dialout group" MISSING "needed to open the NUCLEO's serial port"
    queue "Serial port access (root)" "$USER is not in the dialout group" "sudo usermod -aG dialout $USER     (then log out and back in)"
fi
if [ "$SKIP_FW" = 0 ]; then
    if ls /etc/udev/rules.d/*openocd* /lib/udev/rules.d/*openocd* /etc/udev/rules.d/*stlink* >/dev/null 2>&1; then
        row "ST-LINK udev rule" OK "present"
    else
        row "ST-LINK udev rule" MISSING "OpenOCD needs access to the ST-LINK USB device"
        queue "ST-LINK udev rule (root)" "lets OpenOCD open the ST-LINK without sudo" \
              "printf 'ATTRS{idVendor}==\"0483\", ATTRS{idProduct}==\"374b\", MODE=\"0666\", TAG+=\"uaccess\"\\n' | sudo tee /etc/udev/rules.d/49-stlink.rules && sudo udevadm control --reload && sudo udevadm trigger"
    fi
fi

# ---------------------------------------------------------------------------
# 5. Report
# ---------------------------------------------------------------------------
echo; echo "==> Verification"
printf '%-28s %-8s %s\n' Item Status Detail
for r in "${ROWS[@]}"; do echo "$r"; done
MISSING=$(printf '%s\n' "${ROWS[@]}" | grep -c ' MISSING ')
echo
if [ ${#MANUAL[@]} -gt 0 ]; then
    echo "Queued for you (${#MANUAL[@]} item(s) that need root):"
    i=0; for m in "${MANUAL[@]}"; do i=$((i+1)); echo "  $i. $m"; done
    echo
fi
if [ "$MISSING" = 0 ] && [ ${#MANUAL[@]} = 0 ]; then
    echo "Everything present."
    echo "  GUI:    scripts/client/run_gui.sh      tests: cd client && .venv/bin/python -m pytest"
    [ "$SKIP_FW" = 0 ] && echo "  firmware: cmake --preset firmware-debug && cmake --build --preset firmware-debug"
    exit 0
fi
[ "$MISSING" != 0 ] && echo "$MISSING item(s) missing."
[ ${#MANUAL[@]} -gt 0 ] && exit 3 || exit 2
