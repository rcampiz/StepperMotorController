"""
The client's release, and how it sits against a board's firmware release.

VERSION at the repository root is the single source of the release number: the firmware
reads it at build time (cmake/smc_version.cmake) and this package carries the same number
here and in pyproject.toml; tests/core/test_version.py fails when the copies disagree.

The wire protocol has its own number (protocol_spec.PROTOCOL_VERSION): a board on another
protocol is refused. A board on another release that speaks the same protocol works; a
different major or minor release is only worth a warning (features and defaults differ).
"""

import re
from typing import Optional, Tuple

__version__ = "0.5.0"

_RELEASE = re.compile(r"^\s*v?(\d+)\.(\d+)\.(\d+)")


def parse_release(text: str) -> Optional[Tuple[int, int, int]]:
    """(major, minor, patch) of "0.5.0", "v0.5.0", "0.5.0-sim"; None when it is not one."""
    m = _RELEASE.match(text or "")
    return (int(m.group(1)), int(m.group(2)), int(m.group(3))) if m else None


def release_mismatch(board_fw: str, client: str = __version__) -> Optional[str]:
    """A one-line warning when the board's firmware release differs from this client's in
    major or minor (a patch apart is fine); None when they match or either is unreadable."""
    board, ours = parse_release(board_fw), parse_release(client)
    if board is None or ours is None or board[:2] == ours[:2]:
        return None
    return (f"board firmware {board_fw} and client {client} are different releases; "
            f"update whichever is older")
