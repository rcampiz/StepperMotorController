"""The release number: VERSION at the repository root is the source; the client's copies agree."""

import re
from pathlib import Path

import pytest

import smc_core
from smc_core.sim import FakeBoard
from smc_core.version import __version__, parse_release, release_mismatch

CLIENT = Path(__file__).resolve().parents[2]
VERSION_FILE = CLIENT.parent / "VERSION"


@pytest.mark.skipif(not VERSION_FILE.exists(), reason="not in a repository checkout")
def test_client_carries_the_repository_release():
    release = VERSION_FILE.read_text(encoding="utf-8").strip()
    assert re.fullmatch(r"\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?", release)
    pyproject = (CLIENT / "pyproject.toml").read_text(encoding="utf-8")
    assert re.search(r'^version = "([^"]+)"', pyproject, re.M).group(1) == release, \
        "bump client/pyproject.toml with VERSION"
    assert __version__ == release, "bump client/smc_core/version.py with VERSION"
    assert smc_core.__version__ == release


def test_parse_release():
    assert parse_release("0.5.0") == (0, 5, 0)
    assert parse_release("v1.12.3") == (1, 12, 3)
    assert parse_release("0.5.0-sim") == (0, 5, 0)
    assert parse_release("unknown") is None


def test_release_mismatch_warns_on_major_or_minor_only():
    assert release_mismatch("0.5.0", client="0.5.0") is None
    assert release_mismatch("0.5.3", client="0.5.0") is None          # a patch apart is fine
    assert release_mismatch("0.5.0-sim", client="0.5.0") is None
    assert "0.4.0" in release_mismatch("0.4.0", client="0.5.0")
    assert release_mismatch("1.5.0", client="0.5.0") is not None
    assert release_mismatch("garbage", client="0.5.0") is None        # unreadable: say nothing


def test_the_fake_board_reports_this_release():
    assert FakeBoard.FW == __version__ + "-sim"
    assert release_mismatch(FakeBoard.FW) is None
