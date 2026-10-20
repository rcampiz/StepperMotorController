# Versioning

Two components, each with its own version `MAJOR.MINOR.PATCH`
([Semantic Versioning](https://semver.org/)):

| Component | What it is | Release written in | Tags | Changelog | Counts as a change |
|---|---|---|---|---|---|
| **Firmware** | the STM32 firmware (and the virtual Nucleo built from it) | `VERSION` | `fw-vX.Y.0` | `CHANGELOG.md` | `server/`, `cmake/`, `CMakeLists.txt`, `CMakePresets.json`, `VERSION` |
| **Client** | GUI, CLI and the shared core (`client/`) | `client/pyproject.toml`, `client/smc_core/version.py` | `client-vX.Y.0` | `client/CHANGELOG.md` | `client/`, except `client/tests/` |

A firmware and a client work together when the client knows the board's **protocol number**
(`PROTOCOL_VERSION`, docs/PROTOCOL.md); the client refuses a board on a protocol it does not
know. The two versions say nothing about each other.

## The rules

| Part | Changes when | How |
|---|---|---|
| **PATCH** (0.6.**3**) | Every commit that touched the component | Counted, never written: the commits that touched the component's paths since its tag. A commit that only touches the client leaves the firmware's version alone, and the other way round. A build with uncommitted changes adds `+dirty`. |
| **MINOR** (0.**7**.0) | Something a user or the other side would notice. Firmware: a new protocol version, a new stored-config format, protocol commands added or removed, a new feature or screen, a changed default. Client: a change in the protocols it supports, command builders or CLI commands added or removed, a new feature or panel. Before 1.0, breaking changes are minor releases too. | `release.py minor fw` / `minor client` |
| **MAJOR** (**1**.0.0) | 1.0 is the first release handed to others as stable (e.g. the robot base driven from the Raspberry Pi). From then on, only breaking changes: commands removed, a stored config that cannot be migrated, a rewiring, a protocol the old client cannot speak. | `release.py major fw` / `major client` |

## What is automated

- **Every build reports its exact version.** Firmware: `*IDN?`, `SYST:VER?`, the `!RESET fw=`
  line and the LCD saver (the number without `+dirty`), computed at build time by
  `cmake/smc_build_id.cmake`. Client: `smc --version`, `smc info`
  (`smc_core.version.full_version()`).
- **The release check** (`release.py check`, run with the other checkers) compares each
  component with its last tag and says when a release is due, and why. The firmware's
  triggers: the protocol version, the stored config format, the protocol command set. The
  client's: its supported protocols, its command builders, its CLI commands. Removing
  something is breaking (a major release from 1.0 on). `--strict` makes the check fail instead
  of warn.
- **Cutting a release** (`release.py minor|major fw|client`) needs a clean tree. It bumps the
  component's version files, stamps the Unreleased notes of its changelog with the version and
  the date, commits `Release <component> X.Y.0` and creates the annotated tag. Push with
  `git push --follow-tags` so the tag reaches GitHub.

## Record keeping

- **Tags** mark every release; `release.py which fw 0.6.3` names the commit behind a version
  (the third firmware commit after `fw-v0.6.0`).
- **The changelogs** keep the human-readable history: notes go under `## [Unreleased]` in the
  component's changelog as work lands, and the release stamps them.
- **The board says what it runs:** `*IDN?` or a glance at the saver, then `release.py which`.

## Examples

```
python scripts/release/release.py status               # firmware 0.6.3 / client 0.6.1, releases due?
python scripts/release/release.py minor client         # client 0.7.0: version files, changelog, commit, tag
python scripts/release/release.py which fw 0.6.3       # 1a2b3c4 2026-10-21 <subject>
```
