#!/usr/bin/env python3
"""
start_gui.py: start the GUI, setting it up first where it needs it. Standard library only, so it
runs on a fresh Raspberry Pi or PC:

    python3 start_gui.py [GUI arguments]        (Windows: py start_gui.py)
    python3 start_gui.py --check                (say what it would set up, change nothing)

1. A private Python environment, client/.venv: nothing system-wide changes. Made once.
2. The GUI's packages from the Python Package Index (pypi.org) into it: the missing ones are
   listed with their download size and installed when you say yes.
3. Linux (the Raspberry Pi): the system pieces, each asked first (sudo or a password window):
   Qt's X11 library (libxcb-cursor0), access to a gamepad's input device (the `input` group),
   python3-venv if the environment cannot be made, and an entry in the applications menu.
4. The GUI, from the environment.

Every download and every system change is asked for first: in the terminal when there is one,
otherwise in a small window (tkinter); with neither, nothing is changed and the reason is
printed. A "no" to an optional step is remembered (client/.venv/smc_launcher.json).

Qt for Python publishes Raspberry Pi (aarch64) builds for glibc 2.39 and later: Raspberry Pi
OS based on Debian Trixie runs the GUI, Bookworm (glibc 2.36) cannot install it; there the
command line (python3 -m smc_cli) still works.
"""

import ctypes.util
import glob
import importlib.util
import json
import os
import platform
import queue
import shutil
import subprocess
import sys
import threading
import venv
from typing import Callable, List, Optional, Sequence, Tuple

HERE = os.path.dirname(os.path.abspath(__file__))
VENV = os.path.join(HERE, ".venv")
STATE = os.path.join(VENV, "smc_launcher.json")
TITLE = "Stepper Motor Controller"

# (import name, pip requirement, download in MB, roughly)
PACKAGES: List[Tuple[str, str, int]] = [
    ("serial", "pyserial>=3.5", 1),
    ("PySide6", "PySide6-Essentials>=6.5", 85),      # QtCore / QtGui / QtWidgets / QtSvg: all the GUI uses
    ("pyqtgraph", "pyqtgraph>=0.13", 2),
    ("numpy", "numpy>=1.24", 15),
    ("scipy", "scipy>=1.10", 35),
    ("PIL", "Pillow>=10.0", 5),
    ("pygame", "pygame-ce>=2.5", 10),                 # gamepads: a PS4 pad over USB or Bluetooth
]
QT_ARM_MIN_GLIBC = (2, 39)


# --- asking and running ------------------------------------------------------------------------
class Ui:
    """Questions and long commands: the terminal when there is one, else a tkinter window."""

    def __init__(self):
        self.tty = sys.stdin is not None and sys.stdin.isatty()
        self._tk = None

    def _root(self):
        if self._tk is None:
            self._tk = False
            if os.name == "nt" or os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY"):
                try:
                    import tkinter
                    root = tkinter.Tk()
                    root.withdraw()
                    self._tk = root
                except Exception:              # noqa: BLE001 (no tkinter, or no display)
                    self._tk = False
        return self._tk or None

    def say(self, text: str) -> None:
        print(text)
        if not self.tty and self._root():
            from tkinter import messagebox
            messagebox.showinfo(TITLE, text)

    def ask(self, text: str) -> bool:
        if self.tty:
            while True:
                a = input("\n" + text + " [Y/n] ").strip().lower()
                if a in ("", "y", "yes"):
                    return True
                if a in ("n", "no"):
                    return False
        if self._root():
            from tkinter import messagebox
            return bool(messagebox.askyesno(TITLE, text))
        print(text + "\n(no terminal or window to ask in: not done)")
        return False

    def run(self, cmd: Sequence[str], title: str) -> int:
        """Run cmd, its output where the user can see it; its exit code."""
        print("$ " + " ".join(cmd), flush=True)
        if self.tty or not self._root():
            return subprocess.call(list(cmd))
        return self._run_window(cmd, title)

    def _run_window(self, cmd: Sequence[str], title: str) -> int:
        import tkinter
        root = self._root()
        win = tkinter.Toplevel(root)
        win.title(TITLE + ": " + title)
        text = tkinter.Text(win, width=100, height=24, bg="#0b0f17", fg="#d8e0ff")
        text.pack(fill="both", expand=True)
        lines: "queue.Queue[Optional[str]]" = queue.Queue()
        proc = subprocess.Popen(list(cmd), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)

        def reader():
            for line in proc.stdout:
                lines.put(line)
            lines.put(None)

        threading.Thread(target=reader, daemon=True).start()
        done = []

        def pump():
            try:
                while True:
                    line = lines.get_nowait()
                    if line is None:
                        done.append(proc.wait())
                        win.destroy()
                        root.quit()
                        return
                    text.insert("end", line)
                    text.see("end")
            except queue.Empty:
                pass
            win.after(50, pump)

        win.after(50, pump)
        root.mainloop()
        return done[0] if done else proc.wait()

    def close(self) -> None:
        if self._tk:
            self._tk.destroy()
            self._tk = False


def load_state() -> dict:
    try:
        with open(STATE, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def save_state(state: dict) -> None:
    try:
        with open(STATE, "w", encoding="utf-8") as f:
            json.dump(state, f, indent=1)
    except OSError:
        pass


def optional_step(ui: Ui, state: dict, key: str, question: str, act: Callable[[], bool]) -> None:
    """Ask once; a no is remembered so the launcher does not nag."""
    if key in state.get("declined", []):
        return
    if ui.ask(question):
        act()
    else:
        state.setdefault("declined", []).append(key)
        save_state(state)


# --- what is missing ---------------------------------------------------------------------------
def venv_python() -> str:
    return os.path.join(VENV, "Scripts", "python.exe") if os.name == "nt" else os.path.join(VENV, "bin", "python")


def in_venv() -> bool:
    return os.path.normcase(os.path.abspath(sys.prefix)) == os.path.normcase(os.path.abspath(VENV))


def missing_packages(find_spec: Callable[[str], object] = importlib.util.find_spec) -> List[Tuple[str, str, int]]:
    return [p for p in PACKAGES if find_spec(p[0]) is None]


def glibc() -> Optional[Tuple[int, int]]:
    lib, ver = platform.libc_ver()
    if lib != "glibc" or not ver:
        return None
    try:
        major, minor = ver.split(".")[:2]
        return int(major), int(minor)
    except ValueError:
        return None


def qt_installable(machine: Optional[str] = None, libc: Optional[Tuple[int, int]] = None) -> bool:
    """Qt for Python has a build for this machine: everywhere but ARM Linux older than glibc 2.39
    (Raspberry Pi OS Bookworm)."""
    machine = (machine or platform.machine()).lower()
    if not sys.platform.startswith("linux") and libc is None:
        return True
    if machine not in ("aarch64", "arm64", "armv7l", "armv6l"):
        return True
    if machine in ("armv7l", "armv6l"):
        return False                                   # 32-bit ARM: no Qt for Python wheels at all
    libc = libc if libc is not None else glibc()
    return libc is not None and libc >= QT_ARM_MIN_GLIBC


def sudo(cmd: Sequence[str], ui: Ui) -> List[str]:
    """cmd as root: sudo in a terminal, a password window (pkexec) otherwise."""
    if os.geteuid() == 0:
        return list(cmd)
    if not ui.tty and shutil.which("pkexec"):
        return ["pkexec"] + list(cmd)
    return ["sudo"] + list(cmd)


def apt_install(ui: Ui, packages: Sequence[str], why: str) -> bool:
    if not shutil.which("apt-get"):
        ui.say("Install %s with your system's package manager (%s)." % (" ".join(packages), why))
        return False
    return ui.run(sudo(["apt-get", "install", "-y"] + list(packages), ui), "installing " + " ".join(packages)) == 0


def pad_readable() -> bool:
    """A gamepad's input device can be opened (the `input` group, or the desktop seat's access)."""
    nodes = glob.glob("/dev/input/event*")
    return not nodes or any(os.access(n, os.R_OK) for n in nodes)


def desktop_entry() -> str:
    python = getattr(sys, "_base_executable", None) or shutil.which("python3") or sys.executable
    return ("[Desktop Entry]\nType=Application\nName=%s\nComment=GUI for the STM32 stepper motor controller\n"
            "Exec=\"%s\" \"%s\"\nPath=%s\nTerminal=false\nCategories=Development;Engineering;\n"
            "Icon=applications-engineering\n" % (TITLE, python, os.path.join(HERE, "start_gui.py"), HERE))


# --- the steps ---------------------------------------------------------------------------------
def make_venv(ui: Ui) -> bool:
    try:
        venv.EnvBuilder(with_pip=True).create(VENV)
        return os.path.exists(venv_python())
    except Exception as e:                            # noqa: BLE001 (Debian: no ensurepip without python3-venv)
        shutil.rmtree(VENV, ignore_errors=True)
        if sys.platform.startswith("linux") and ui.ask(
                "Making the environment failed (%s).\nDebian and Raspberry Pi OS need the python3-venv package for it. "
                "Install it now (sudo)?" % e):
            if apt_install(ui, ["python3-venv"], "making a Python environment"):
                venv.EnvBuilder(with_pip=True).create(VENV)
                return os.path.exists(venv_python())
        return False


def linux_system_steps(ui: Ui, state: dict) -> None:
    if not sys.platform.startswith("linux"):
        return
    if ctypes.util.find_library("xcb-cursor") is None:
        optional_step(ui, state, "libxcb-cursor0",
                      "Qt's X11 display plugin needs the system library libxcb-cursor0 (about 50 kB).\n"
                      "Install it now (sudo)?",
                      lambda: apt_install(ui, ["libxcb-cursor0"], "Qt's X11 plugin"))
    if not pad_readable():
        user = os.environ.get("USER", "")
        optional_step(ui, state, "input-group",
                      "Reading a gamepad (a PS4 controller) needs access to /dev/input: add %s to the input group "
                      "(sudo; it takes effect at the next login)?" % (user or "you"),
                      lambda: ui.run(sudo(["usermod", "-aG", "input", user], ui), "input group") == 0)
    entry = os.path.expanduser("~/.local/share/applications/smc-gui.desktop")
    if not os.path.exists(entry):
        def write():
            os.makedirs(os.path.dirname(entry), exist_ok=True)
            with open(entry, "w", encoding="utf-8") as f:
                f.write(desktop_entry())
            return True
        optional_step(ui, state, "menu-entry", "Add \"%s\" to the applications menu?" % TITLE, write)


def check(ui: Ui) -> int:
    """--check: what the launcher would do here, changing nothing."""
    print("environment:", VENV, "(exists)" if os.path.exists(venv_python()) else "(would be made)")
    py = venv_python() if os.path.exists(venv_python()) else sys.executable
    probe = "import importlib.util,json;print(json.dumps([n for n in %r if importlib.util.find_spec(n) is None]))" % (
        [p[0] for p in PACKAGES],)
    try:
        gone = json.loads(subprocess.check_output([py, "-c", probe], text=True))
    except (subprocess.CalledProcessError, OSError, ValueError):
        gone = [p[0] for p in PACKAGES]
    need = [p for p in PACKAGES if p[0] in gone]
    print("packages to download:", ", ".join(p[1] for p in need) or "none",
          "(about %d MB)" % sum(p[2] for p in need) if need else "")
    print("Qt for Python installable here:", qt_installable())
    if sys.platform.startswith("linux"):
        print("libxcb-cursor0:", "present" if ctypes.util.find_library("xcb-cursor") else "missing")
        print("gamepad devices readable:", pad_readable())
    return 0


def main(argv: List[str]) -> int:
    ui = Ui()
    try:
        if "--check" in argv:
            return check(ui)
        if not in_venv():
            if not os.path.exists(venv_python()):
                if not ui.ask("The GUI runs from a private Python environment:\n  %s\n"
                              "(made once; nothing system-wide changes). Make it now?" % VENV):
                    return 1
                if not make_venv(ui):
                    ui.say("Could not make the environment in %s." % VENV)
                    return 1
            linux_system_steps(ui, load_state())
            ui.close()
            # carry on inside the environment
            return subprocess.call([venv_python(), os.path.abspath(__file__)] + argv)

        need = missing_packages()
        if need:
            if any(p[0] == "PySide6" for p in need) and not qt_installable():
                ui.say("Qt for Python has no build for this system (%s, glibc %s): Raspberry Pi OS Bookworm cannot run the "
                       "GUI. Raspberry Pi OS based on Debian Trixie can. The command line works here: "
                       "python3 -m smc_cli --help" % (platform.machine(), ".".join(map(str, glibc() or ()))))
                return 1
            total = sum(p[2] for p in need)
            if not ui.ask("The GUI needs these from the Python Package Index (pypi.org), about %d MB to download, "
                          "installed into client/.venv:\n  %s\nDownload and install them now?"
                          % (total, "\n  ".join(p[1] for p in need))):
                return 1
            rc = ui.run([sys.executable, "-m", "pip", "install", "--upgrade", "pip"], "updating pip")
            rc = rc or ui.run([sys.executable, "-m", "pip", "install"] + [p[1] for p in need], "installing the GUI's packages")
            if rc != 0 or missing_packages():
                ui.say("The install did not finish (see the output above). Nothing else was changed; run start_gui.py "
                       "again to retry.")
                return rc or 1
        ui.close()
        return subprocess.call([sys.executable, "-m", "smc_gui"] + argv, cwd=HERE)
    finally:
        ui.close()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
