#!/usr/bin/env python3
"""
check_includes.py — Namespace boundary enforcement for STM32 firmware.

Scans all .cpp and .hpp files under server/ and verifies that #include
directives respect the namespace adjacency rules defined in CODE_PHILOSOPHY.md
Section 14.

Usage:
    python scripts/check_includes.py          # report violations
    python scripts/check_includes.py --strict # exit nonzero on any violation

Namespace adjacency (all cross-namespace deps go through harness/pins/):

    System::    <-> Harness <-> Services::
    Services::  <-> Harness <-> Scheduler::
    Transport:: <-> Harness <-> Protocol::
    Protocol::  <-> Harness <-> Services::
    Services::  <-> Harness <-> Drivers::
    Drivers::   <-> Harness <-> Board::
"""

import os
import re
import sys

# ── Directory → Namespace mapping ──────────────────────────────────────────

def classify(filepath):
    """Determine namespace from file path."""
    fp = filepath.replace("\\", "/")

    # Order matters — more specific paths first
    if "system_init" in fp or fp.endswith("main.cpp"):
        return "System"
    if "/harness/" in fp:
        return "Harness"
    if "/wiring/" in fp:
        return "Wiring"
    if "/F_platform/tasks/" in fp:
        return "Scheduler"
    if "/F_platform/" in fp:
        return "Platform"
    if "/L1_transport/" in fp:
        return "Transport"
    if "/L2_protocol/" in fp:
        return "Protocol"
    if "/L3_services/" in fp:
        return "Services"
    if "/L4_drivers/" in fp:
        return "Drivers"
    if "/L5_board/" in fp:
        return "Board"
    return None  # skip unknown files

# ── Allowed include prefixes per namespace ─────────────────────────────────

ALLOWED = {
    "Transport": ["L1_transport/", "harness/"],
    "Protocol":  ["L2_protocol/", "harness/"],
    "Services":  ["L3_services/", "harness/"],
    "Drivers":   ["L4_drivers/", "harness/", "X_middlewares/"],
    "Board":     ["L5_board/", "harness/", "X_vendor/", "X_middlewares/"],
    "Scheduler": ["F_platform/", "harness/"],
    "Platform":  ["F_platform/", "harness/", "X_vendor/", "X_middlewares/"],
    "Harness":   None,  # Harness can include anything (it IS the bridge)
    "System":    ["harness/", "L3_services/", "F_platform/", "system_init"],
    "Wiring":    None,  # Composition root can include anything
}

# ── CMSIS rule: only Board and Platform may include CMSIS ──────────────────

CMSIS_PATTERNS = [
    re.compile(r"X_vendor/CMSIS/stm32"),
    re.compile(r"X_vendor/CMSIS/core_cm"),
]
CMSIS_ALLOWED_NS = {"Board", "Platform", "Harness"}

# ── Cross-boundary prefixes ────────────────────────────────────────────────
# Includes that start with one of these are cross-boundary references and must
# be checked.  Includes that don't match any prefix (e.g. "ui/screen.hpp"
# resolved by -Iserver/layers/L3_services) are same-layer includes via the
# build system's -I flags and are skipped.

CROSS_BOUNDARY_PREFIXES = [
    "L1_transport/", "L2_protocol/", "L3_services/", "L4_drivers/", "L5_board/",
    "F_platform/", "F_util/",
    "harness/",
    "X_vendor/", "X_middlewares/",
    "system_init",
]

# ── Scanner ────────────────────────────────────────────────────────────────

INCLUDE_RE = re.compile(r'^\s*#include\s+"([^"]+)"')

def check_file(filepath, server_root):
    """Check a single file for include violations. Returns list of (line_num, include, reason)."""
    ns = classify(filepath)
    if ns is None:
        return []

    violations = []
    allowed_prefixes = ALLOWED.get(ns)

    try:
        with open(filepath, "r", encoding="utf-8", errors="replace") as f:
            for line_num, line in enumerate(f, 1):
                m = INCLUDE_RE.match(line)
                if not m:
                    continue
                inc = m.group(1)

                # Skip includes without a path separator (local "foo.hpp")
                if "/" not in inc:
                    continue

                # Skip includes that don't start with a known cross-boundary
                # prefix — these are same-layer includes resolved by the
                # compiler's -I flags (e.g. "ui/screen.hpp" inside L3_services)
                if not any(inc.startswith(p) for p in CROSS_BOUNDARY_PREFIXES):
                    continue

                # CMSIS check
                if ns not in CMSIS_ALLOWED_NS:
                    for pat in CMSIS_PATTERNS:
                        if pat.search(inc):
                            violations.append((line_num, inc,
                                f"CMSIS include in {ns}:: (only Board/Platform allowed)"))
                            break

                # Namespace boundary check
                if allowed_prefixes is not None:
                    if not any(inc.startswith(prefix) for prefix in allowed_prefixes):
                        violations.append((line_num, inc,
                            f"{ns}:: may not include '{inc}' "
                            f"(allowed: {', '.join(allowed_prefixes)})"))

    except (IOError, OSError):
        pass

    return violations


def main():
    strict = "--strict" in sys.argv

    # Find server root
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    server_root = os.path.join(project_root, "server")

    if not os.path.isdir(server_root):
        print(f"ERROR: server/ directory not found at {server_root}")
        sys.exit(1)

    total_violations = 0
    violation_files = 0

    for dirpath, dirnames, filenames in os.walk(server_root):
        # Skip external/ directory
        rel = os.path.relpath(dirpath, server_root).replace("\\", "/")
        if rel.startswith("external"):
            dirnames.clear()
            continue

        for fname in filenames:
            if not (fname.endswith(".cpp") or fname.endswith(".hpp") or fname.endswith(".h")):
                continue

            fpath = os.path.join(dirpath, fname)
            violations = check_file(fpath, server_root)

            if violations:
                violation_files += 1
                rel_path = os.path.relpath(fpath, project_root).replace("\\", "/")
                for line_num, inc, reason in violations:
                    total_violations += 1
                    print(f"  {rel_path}:{line_num}: {reason}")

    # Summary
    print()
    if total_violations == 0:
        print("No include violations found.")
    else:
        print(f"{total_violations} violation(s) in {violation_files} file(s).")

    if strict and total_violations > 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
