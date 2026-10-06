"""CLIENT_REVIEW section 9 rules, enforced on the frontends' source.

Rule 1: a frontend never formats a command string or parses a reply line; it builds
Requests with smc_core.commands and renders smc_core.views / codec data.
Rule 2: nothing in smc_core imports Qt.
"""

import ast
import pathlib
import re

CLIENT = pathlib.Path(__file__).resolve().parent.parent
# A string that starts like a command line. Tooltips and messages that merely mention
# a command ("Clear trim integrator (CTRL:TRIM:RESET)") are fine.
SCPI = re.compile(r"^\s*(\*IDN\?|(MOT|SYST|SYNC|DIAG|CTRL|DEV|UI|DBG|DRV):[A-Z_:?]*)(\s|$)")


def _code_strings(path: pathlib.Path):
    """String constants in code (docstrings excluded), with line numbers."""
    tree = ast.parse(path.read_text(encoding="utf-8"))
    docstrings = set()
    for node in ast.walk(tree):
        if isinstance(node, (ast.Module, ast.ClassDef, ast.FunctionDef, ast.AsyncFunctionDef)):
            body = getattr(node, "body", [])
            if body and isinstance(body[0], ast.Expr) and isinstance(body[0].value, ast.Constant):
                docstrings.add(id(body[0].value))
    for node in ast.walk(tree):
        if isinstance(node, ast.Constant) and isinstance(node.value, str) and id(node) not in docstrings:
            yield node.lineno, node.value


def test_gui_builds_no_command_strings():
    offenders = []
    for path in sorted((CLIENT / "smc_gui").glob("*.py")):
        for line, text in _code_strings(path):
            if SCPI.search(text):
                offenders.append(f"{path.name}:{line}: {text!r}")
    assert not offenders, "frontends must use smc_core.commands:\n" + "\n".join(offenders)


def test_gui_does_not_parse_reply_text():
    offenders = [p.name for p in (CLIENT / "smc_gui").glob("*_panel.py")
                 if re.search(r"^import re$", p.read_text(encoding="utf-8"), re.M)]
    assert not offenders, f"panels must render smc_core.views data, not regex replies: {offenders}"


def test_core_has_no_qt():
    for path in (CLIENT / "smc_core").rglob("*.py"):
        for node in ast.walk(ast.parse(path.read_text(encoding="utf-8"))):
            names = ([a.name for a in node.names] if isinstance(node, ast.Import) else
                     [node.module or ""] if isinstance(node, ast.ImportFrom) else [])
            assert not any(n.startswith(("PySide", "PyQt", "shiboken")) for n in names), path
