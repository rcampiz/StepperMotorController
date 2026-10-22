"""The GUI's launcher (client/start_gui.py): what it can install where, what is missing, the menu
entry, and a declined step remembered. Nothing is downloaded or changed."""

import importlib.util
import os

import pytest

HERE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
spec = importlib.util.spec_from_file_location("start_gui", os.path.join(HERE, "start_gui.py"))
start_gui = importlib.util.module_from_spec(spec)
spec.loader.exec_module(start_gui)


@pytest.mark.parametrize("machine, libc, ok", [
    ("aarch64", (2, 36), False),        # Raspberry Pi OS Bookworm: no Qt for Python build
    ("aarch64", (2, 41), True),         # Raspberry Pi OS Trixie
    ("armv7l", (2, 41), False),         # 32-bit ARM: none at all
    ("x86_64", (2, 31), True),
])
def test_qt_for_python_is_installable_where_it_has_a_build(machine, libc, ok, monkeypatch):
    monkeypatch.setattr(start_gui.sys, "platform", "linux")
    assert start_gui.qt_installable(machine, libc) is ok


def test_the_missing_packages_are_listed_by_their_pip_names():
    have = {"serial", "numpy"}
    need = start_gui.missing_packages(lambda name: object() if name in have else None)
    names = [p[1] for p in need]
    assert "pyserial>=3.5" not in names and "PySide6-Essentials>=6.5" in names and "pygame-ce>=2.5" in names
    assert start_gui.missing_packages(lambda name: object()) == []


def test_the_menu_entry_starts_the_launcher_from_the_client_folder():
    entry = start_gui.desktop_entry()
    assert "Name=Stepper Motor Controller" in entry
    assert os.path.join(start_gui.HERE, "start_gui.py") in entry and "Path=%s" % start_gui.HERE in entry
    assert "Terminal=false" in entry


class NoUi:
    def __init__(self, answer):
        self.answer = answer
        self.asked = 0

    def ask(self, text):
        self.asked += 1
        return self.answer


def test_a_declined_step_is_not_asked_again(tmp_path, monkeypatch):
    monkeypatch.setattr(start_gui, "STATE", str(tmp_path / "state.json"))
    ui, done = NoUi(False), []
    state = start_gui.load_state()
    start_gui.optional_step(ui, state, "menu-entry", "Add it?", lambda: done.append(1))
    start_gui.optional_step(ui, start_gui.load_state(), "menu-entry", "Add it?", lambda: done.append(1))
    assert ui.asked == 1 and done == []
    yes = NoUi(True)
    start_gui.optional_step(yes, start_gui.load_state(), "input-group", "Join?", lambda: done.append(1))
    assert done == [1]
