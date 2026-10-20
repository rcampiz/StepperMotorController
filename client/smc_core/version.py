"""
The client's version (GUI, CLI, core), independent of the firmware's (docs/VERSIONING.md).

The release MAJOR.MINOR.0 is written here and in pyproject.toml (tests/core/test_version.py
checks they agree); `scripts/release/release.py minor client` bumps both. The patch is counted,
never written: the commits that touched client/ (its tests excepted) since the tag
client-vMAJOR.MINOR.0, which full_version() gives for the checkout this package runs from.

Whether a client and a board work together is the protocol number's job
(protocol_spec.PROTOCOL_VERSION, Board.SUPPORTED_PROTOCOLS), not the versions'.
"""

import functools
import re
import subprocess
from pathlib import Path
from typing import Optional, Tuple

__version__ = "0.6.0"

TAG_PREFIX = "client-v"
#: What counts as a client change (git pathspecs, from the repository root)
PATHS = ("client", ":(exclude)client/tests")

_RELEASE = re.compile(r"^\s*v?(\d+)\.(\d+)\.(\d+)")


def parse_release(text: str) -> Optional[Tuple[int, int, int]]:
    """(major, minor, patch) of "0.5.0", "v0.5.0", "0.5.3+dirty"; None when it is not one."""
    m = _RELEASE.match(text or "")
    return (int(m.group(1)), int(m.group(2)), int(m.group(3))) if m else None


@functools.lru_cache(maxsize=None)
def full_version(base: str = __version__, repo: Optional[str] = None) -> str:
    """MAJOR.MINOR.<client commits since client-vMAJOR.MINOR.0>[+dirty] for the checkout this
    package runs from; `base` when there is no git, no checkout or no tag (an installed package)."""
    rel = parse_release(base)
    root = Path(repo) if repo else Path(__file__).resolve().parents[2]
    if rel is None or not (root / ".git").exists():
        return base
    tag = f"{TAG_PREFIX}{rel[0]}.{rel[1]}.0"
    try:
        run = dict(cwd=root, capture_output=True, text=True, timeout=5)
        count = subprocess.run(["git", "rev-list", "--count", f"{tag}..HEAD", "--", *PATHS], **run)
        if count.returncode != 0 or not count.stdout.strip().isdigit():
            return base
        dirty = subprocess.run(["git", "diff", "--quiet", "HEAD", "--", *PATHS], **run)
    except (OSError, subprocess.SubprocessError):
        return base
    return f"{rel[0]}.{rel[1]}.{count.stdout.strip()}" + ("+dirty" if dirty.returncode != 0 else "")
