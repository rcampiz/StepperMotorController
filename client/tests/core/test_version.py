"""The client's version: its own release (pyproject.toml and smc_core.version agree) and the
patch counted from git (docs/VERSIONING.md)."""

import re
from pathlib import Path

import smc_core
from smc_core.sim import FakeBoard
from smc_core.version import __version__, full_version, parse_release

CLIENT = Path(__file__).resolve().parents[2]


def test_pyproject_and_the_package_carry_the_same_release():
    assert re.fullmatch(r"\d+\.\d+\.0", __version__), "the written release is MAJOR.MINOR.0"
    pyproject = (CLIENT / "pyproject.toml").read_text(encoding="utf-8")
    assert re.search(r'^version = "([^"]+)"', pyproject, re.M).group(1) == __version__, \
        "bump client/pyproject.toml with smc_core/version.py (release.py minor client does both)"
    assert smc_core.__version__ == __version__


def test_parse_release():
    assert parse_release("0.5.0") == (0, 5, 0)
    assert parse_release("v1.12.3") == (1, 12, 3)
    assert parse_release("0.5.3+dirty") == (0, 5, 3)
    assert parse_release("unknown") is None


def test_the_fake_board_reports_this_release():
    assert FakeBoard.FW == __version__ + "-sim"


def test_full_version_counts_client_commits_or_falls_back_to_the_release(tmp_path):
    assert full_version("0.6.0", repo=str(tmp_path)) == "0.6.0"     # no checkout: the release
    v = full_version()
    assert re.fullmatch(r"\d+\.\d+\.\d+(\+dirty)?", v), v
    assert parse_release(v)[:2] == parse_release(__version__)[:2]     # same MAJOR.MINOR
