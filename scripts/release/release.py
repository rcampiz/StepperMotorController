"""Versions and releases of the firmware and the client (docs/VERSIONING.md).

    python scripts/release/release.py status                 both versions; is a release due?
    python scripts/release/release.py check [--strict]       the same, for the checker list
                                                             (--strict: exit 1 when one is due)
    python scripts/release/release.py minor|major fw|client [--date YYYY-MM-DD] [--trailer TEXT] [--dry-run]
                                                             cut a release: version files,
                                                             changelog, commit, tag
    python scripts/release/release.py which fw|client 0.6.3  the commit a version names

Each component has its own release MAJOR.MINOR.0 (written in its version files) and tags
(fw-vX.Y.0, client-vX.Y.0). The patch is counted, never written: the commits that touched the
component's paths since its tag, so a commit gets a new firmware version only when it touched
the firmware, and a new client version only when it touched the client. The firmware build
(cmake/smc_build_id.cmake) and smc_core.version.full_version() count the same way.
"""
import argparse
import datetime
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RELEASE = re.compile(r"^(\d+)\.(\d+)\.(\d+)$")


class Component:
    def __init__(self, key, name, tag_prefix, paths, version_files, changelog, reasons):
        self.key, self.name, self.tag_prefix = key, name, tag_prefix
        self.paths = paths                    # git pathspecs: what counts as a change
        self.version_files = version_files    # [(path, regex with one group around the version)]
        self.changelog = changelog
        self.reasons = reasons                # fn(tag) -> [(level, why)]


def git(*args, check=True):
    r = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True)
    if check and r.returncode != 0:
        raise SystemExit(f"git {' '.join(args)}: {r.stderr.strip()}")
    return r


def read_text(path):
    return (ROOT / path).read_bytes().decode("utf-8")


def write_like(path, old_text, new_text):
    """Write new_text keeping the file's line endings."""
    nl = "\r\n" if "\r\n" in old_text else "\n"
    (ROOT / path).write_bytes(new_text.replace("\r\n", "\n").replace("\n", nl).encode("utf-8"))


def file_at(rev, path):
    r = git("show", f"{rev}:{path}", check=False)
    return r.stdout if r.returncode == 0 else None


def number(text, pattern):
    m = re.search(pattern, text or "")
    return m.group(1) if m else None


def changed(tag, path, pattern, what):
    then, now = number(file_at(tag, path), pattern), number(read_text(path), pattern)
    return [("minor", f"{what} {then} -> {now}")] if then is not None and now is not None and then != now else []


def command_set_change(tag, names_then, names_now, what):
    out = []
    added, removed = sorted(names_now - names_then), sorted(names_then - names_now)
    if added:
        out.append(("minor", f"{len(added)} {what} added: {', '.join(added[:6])}" + (" ..." if len(added) > 6 else "")))
    if removed:
        out.append(("breaking", f"{len(removed)} {what} removed: {', '.join(removed[:6])}" + (" ..." if len(removed) > 6 else "")))
    return out


def fw_reasons(tag):
    out = changed(tag, "server/harness/pins/protocol_version.hpp", r"PROTOCOL_VERSION\s*=\s*(\d+)", "protocol version")
    out += changed(tag, "server/layers/L3_services/motion/motor_config/motor_config.hpp",
                   r"MOTOR_CONFIG_VERSION\s*=\s*(\d+)", "stored config format")

    def names(text):
        return {c["name"] for c in json.loads(text).get("commands", [])} if text else set()
    out += command_set_change(tag, names(file_at(tag, "docs/protocol.json")), names(read_text("docs/protocol.json")),
                              "protocol command(s)")
    return out


def client_reasons(tag):
    out = changed(tag, "client/smc_core/board.py", r"SUPPORTED_PROTOCOLS\s*=\s*(\([^)]*\))", "supported protocols")

    def names(text):
        return set(re.findall(r'^def ([a-z][a-z0-9_]*)\(', text or "", re.M))   # public builders only
    out += command_set_change(tag, names(file_at(tag, "client/smc_core/commands.py")),
                              names(read_text("client/smc_core/commands.py")), "command builder(s)")

    def subcommands(text):
        return set(re.findall(r'add_parser\("([a-z-]+)"', text or ""))
    out += command_set_change(tag, subcommands(file_at(tag, "client/smc_cli/__main__.py")),
                              subcommands(read_text("client/smc_cli/__main__.py")), "CLI subcommand(s)")
    return out


COMPONENTS = {
    "fw": Component("fw", "firmware", "fw-v",
                    ["server", "cmake", "CMakeLists.txt", "CMakePresets.json", "VERSION"],
                    [("VERSION", r"^(\S+)")], "CHANGELOG.md", fw_reasons),
    "client": Component("client", "client", "client-v",
                        ["client", ":(exclude)client/tests"],
                        [("client/pyproject.toml", r'^version = "([^"]+)"'),
                         ("client/smc_core/version.py", r'^__version__ = "([^"]+)"')],
                        "client/CHANGELOG.md", client_reasons),
}


def release_of(c):
    path, pattern = c.version_files[0]
    text = number(read_text(path), re.compile(pattern, re.M))
    m = RELEASE.match(text or "")
    if not m:
        raise SystemExit(f"{path}: version '{text}', expected MAJOR.MINOR.PATCH")
    return tuple(int(g) for g in m.groups())


def tag_of(c, rel):
    return f"{c.tag_prefix}{rel[0]}.{rel[1]}.0"


def tag_exists(tag):
    return git("rev-parse", "-q", "--verify", f"refs/tags/{tag}", check=False).returncode == 0


def full_version(c):
    rel = release_of(c)
    tag = tag_of(c, rel)
    if not tag_exists(tag):
        return "%d.%d.%d" % rel, None
    n = git("rev-list", "--count", f"{tag}..HEAD", "--", *c.paths).stdout.strip()
    dirty = git("diff", "--quiet", "HEAD", "--", *c.paths, check=False).returncode != 0
    return f"{rel[0]}.{rel[1]}.{n}" + ("+dirty" if dirty else ""), int(n)


def due(rel, reasons):
    """The release the reasons call for: "major" (breaking, from 1.0 on), "minor" or None."""
    if not reasons:
        return None
    if rel[0] >= 1 and any(level == "breaking" for level, _ in reasons):
        return "major"
    return "minor"


def report(c):
    rel = release_of(c)
    tag = tag_of(c, rel)
    version, since = full_version(c)
    print(f"{c.name:8s} {version}")
    if since is None:
        print(f"         {tag} missing: the patch cannot be counted (tag the release commit)")
        return None
    print(f"         {since} {c.name} commit(s) since {tag}")
    reasons = c.reasons(tag)
    level = due(rel, reasons)
    if level:
        print(f"         RELEASE DUE ({level}): scripts/release/release.py {level} {c.key}")
        for lv, why in reasons:
            print(f"           - {why}" + (" [breaking]" if lv == "breaking" else ""))
    else:
        print("         no release due")
    return level


def cut(c, level, date, trailer, dry_run):
    if git("status", "--porcelain", "--untracked-files=no").stdout.strip():
        raise SystemExit("uncommitted changes: commit or stash them before cutting a release")
    rel = release_of(c)
    new = (rel[0] + 1, 0, 0) if level == "major" else (rel[0], rel[1] + 1, 0)
    new_s = "%d.%d.%d" % new
    tag = f"{c.tag_prefix}{new_s}"
    if tag_exists(tag):
        raise SystemExit(f"{tag} already exists")

    edits = []
    for path, pattern in c.version_files:
        old = read_text(path)
        text, n = re.subn(pattern, lambda m: m.group(0).replace(m.group(1), new_s), old, count=1, flags=re.M)
        if n != 1:
            raise SystemExit(f"{path}: no version to bump")
        edits.append((path, old, text))
    old = read_text(c.changelog)
    head = re.search(r"^## \[Unreleased\][ \t]*\r?\n", old, flags=re.M)
    if not head:
        raise SystemExit(f"{c.changelog}: no '## [Unreleased]' heading")
    edits.append((c.changelog, old, old[:head.end()] + f"\n## [{new_s}] - {date}\n" + old[head.end():]))

    print(f"{c.name} release {new_s} ({level}), tag {tag}, dated {date}")
    if dry_run:
        print("dry run: nothing written")
        return
    for path, old_text, new_text in edits:
        write_like(path, old_text, new_text)
    git("add", *[p for p, _, _ in edits])
    message = f"Release {c.name} {new_s}\n\nSee {c.changelog} [{new_s}].\n" + (f"\n{trailer}\n" if trailer else "")
    git("commit", "-q", "-m", message)
    git("tag", "-a", tag, "-m", f"{c.name.capitalize()} release {new_s}")
    print(f"committed and tagged {tag}; push with: git push --follow-tags")


def which(c, version):
    m = re.match(r"^v?(\d+)\.(\d+)\.(\d+)", version)
    if not m:
        raise SystemExit(f"'{version}' is not MAJOR.MINOR.PATCH")
    major, minor, patch = (int(g) for g in m.groups())
    tag = f"{c.tag_prefix}{major}.{minor}.0"
    if not tag_exists(tag):
        raise SystemExit(f"{tag} does not exist")
    if patch == 0:
        rev = git("rev-list", "-n", "1", tag).stdout.strip()
    else:
        after = git("rev-list", "--reverse", f"{tag}..HEAD", "--", *c.paths).stdout.split()
        if patch > len(after):
            raise SystemExit(f"{c.name} {version}: only {len(after)} {c.name} commit(s) after {tag}")
        rev = after[patch - 1]
    print(git("log", "-1", "--format=%h %ad %s", "--date=short", rev).stdout.strip())


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("status")
    p = sub.add_parser("check")
    p.add_argument("--strict", action="store_true", help="exit 1 when a release is due")
    for name in ("minor", "major"):
        p = sub.add_parser(name)
        p.add_argument("component", choices=sorted(COMPONENTS))
        p.add_argument("--date", default=datetime.date.today().isoformat())
        p.add_argument("--trailer", default="", help="a line appended to the release commit message")
        p.add_argument("--dry-run", action="store_true")
    p = sub.add_parser("which")
    p.add_argument("component", choices=sorted(COMPONENTS))
    p.add_argument("version")
    a = ap.parse_args()

    if a.cmd in ("status", "check"):
        levels = [report(c) for c in COMPONENTS.values()]
        if a.cmd == "check" and a.strict and any(levels):
            sys.exit(1)
    elif a.cmd in ("minor", "major"):
        cut(COMPONENTS[a.component], a.cmd, a.date, a.trailer, a.dry_run)
    else:
        which(COMPONENTS[a.component], a.version)


if __name__ == "__main__":
    main()
