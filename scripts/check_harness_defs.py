#!/usr/bin/env python3
"""
check_harness_defs.py -- validate the harness definitions (server/harness/defs/) and the
single-definition rule for boundary interfaces.

Checks
  1. Every harness header harness_<a>_<b>.hpp includes only: harness/defs/harness_signal.hpp,
     harness/pins/, F_platform/boundary/ (power net), and the boundary/ dirs of its two layers.
  2. Each *_SIGNALS table's row count equals the count stored in its *_DESC.
  3. Every connect*() declared in a harness header is defined in harness/defs/harness_connect.cpp.
  4. Single definition: each boundary interface class name (class I* with pure virtuals) is
     defined in exactly one <layer>/boundary/ directory.
  5. harness/pins/ holds only shared PODs / enums / init contracts; the only abstract class
     allowed there is Harness::IResponseSink.
  6. ENABLE_INTERFACE_TRACE and harness/taps/ appear only under harness/ (taps are applied
     solely in harness/defs/harness_connect.cpp).
  7. No server/harness/adapters/ directory and no duplicate consumer-port headers remain.
  8. No forwarder in harness/pins/: a pins header must not merely re-include a layer's
     boundary/ header or a task-sheet header (consumers include the defs/ harness header
     or, within the same layer, the boundary header itself).

Usage: python scripts/check_harness_defs.py [--strict]
"""
import os
import re
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SERVER = os.path.join(ROOT, "server")
DEFS = os.path.join(SERVER, "harness", "defs")

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*"([^"]+)"')
CONNECT_DECL_RE = re.compile(r'\b(connect[A-Z]\w*)\s*\(')
SIGNALS_RE = re.compile(r'constexpr\s+SignalDesc\s+(\w+)\[\]\s*=\s*\{(.*?)\};', re.S)
DESC_RE = re.compile(r'constexpr\s+HarnessDesc\s+(\w+)\s*=\s*\{\s*"([^"]+)"\s*,\s*"[^"]*"\s*,\s*"[^"]*"\s*,\s*(\w+)\s*,\s*(\d+)\s*\}')
CLASS_RE = re.compile(r'^\s*class\s+(I[A-Z]\w*)\b', re.M)
PURE_RE = re.compile(r'\)\s*(const\s*)?=\s*0\s*;')

LAYER_DIRS = {
    "L1": "L1_transport/", "L2": "L2_protocol/", "L3": "L3_services/",
    "L4": "L4_drivers/", "L5": "L5_board/", "TASKS": "F_platform/tasks/", "RTOS": "F_platform/",
}
# harness file -> the two layers it may reach into
HARNESS_LAYERS = {
    "harness_l1_l2.hpp": ("L1", "L2"),
    "harness_l2_l3.hpp": ("L2", "L3"),
    "harness_l3_l4.hpp": ("L3", "L4"),
    "harness_l4_l5.hpp": ("L4", "L5"),
    "harness_tasks_l3.hpp": ("TASKS", "L3"),
    "harness_l5_l1_bytes.hpp": ("L5", "L1"),
}
ALWAYS_OK = ("harness/defs/harness_signal.hpp", "harness/pins/", "F_platform/boundary/")


def read(path):
    with open(path, encoding="utf-8", errors="ignore") as fh:
        return fh.read()


def walk(root, exts=(".hpp", ".cpp", ".h")):
    for d, _, files in os.walk(root):
        if "external" in d.replace("\\", "/"):
            continue
        for f in files:
            if f.endswith(exts):
                yield os.path.join(d, f)


def main():
    strict = "--strict" in sys.argv
    errors = []

    # ---- 1-3: harness definition headers
    connect_decls = set()
    for name, (a, b) in HARNESS_LAYERS.items():
        path = os.path.join(DEFS, name)
        if not os.path.exists(path):
            continue
        text = read(path)
        allowed = ALWAYS_OK + tuple(LAYER_DIRS[x] + "boundary/" for x in (a, b))
        for ln, line in enumerate(text.splitlines(), 1):
            m = INCLUDE_RE.match(line)
            if not m:
                continue
            inc = m.group(1)
            if not any(inc.startswith(p) for p in allowed):
                errors.append(f"{name}:{ln}: harness header may not include '{inc}'")
        tables = {m.group(1): m.group(2) for m in SIGNALS_RE.finditer(text)}
        for m in DESC_RE.finditer(text):
            desc, hid, table, count = m.group(1), m.group(2), m.group(3), int(m.group(4))
            rows = len(re.findall(r'^\s*\{\s*"', tables.get(table, ""), re.M))
            if rows != count:
                errors.append(f"{name}: {desc} says {count} signals but {table} has {rows} rows")
        for m in CONNECT_DECL_RE.finditer(text):
            connect_decls.add(m.group(1))

    connect_cpp = read(os.path.join(DEFS, "harness_connect.cpp")) if os.path.exists(os.path.join(DEFS, "harness_connect.cpp")) else ""
    for fn in sorted(connect_decls):
        if not re.search(r'\b' + fn + r'\s*\([^;]*\)\s*\{', connect_cpp, re.S):
            errors.append(f"harness_connect.cpp: no definition for {fn}()")

    # ---- 4: single definition of boundary interfaces
    homes = defaultdict(list)
    for path in walk(SERVER, (".hpp",)):
        rel = os.path.relpath(path, SERVER).replace("\\", "/")
        if "/boundary/" not in rel:
            continue
        text = read(path)
        if not PURE_RE.search(text):
            continue
        for m in CLASS_RE.finditer(text):
            homes[m.group(1)].append(rel)
    for cls, files in sorted(homes.items()):
        if len(files) > 1:
            errors.append(f"interface {cls} defined in more than one boundary dir: {', '.join(files)}")

    # ---- 5: pins hold no abstract classes except IResponseSink
    pins = os.path.join(SERVER, "harness", "pins")
    for path in walk(pins, (".hpp",)):
        text = read(path)
        if PURE_RE.search(text):
            names = [c for c in CLASS_RE.findall(text) if c != "IResponseSink"]
            if names:
                errors.append(f"harness/pins/{os.path.basename(path)}: abstract class(es) {names} belong in a layer's boundary/")

    # ---- 6: trace ifdefs / taps only inside harness (and the composition root)
    for path in walk(SERVER):
        rel = os.path.relpath(path, SERVER).replace("\\", "/")
        if rel.startswith("harness/"):
            continue
        text = read(path)
        if "ENABLE_INTERFACE_TRACE" in text:
            errors.append(f"{rel}: ENABLE_INTERFACE_TRACE outside harness/")
        if "harness/taps/" in text:
            errors.append(f"{rel}: includes harness/taps/ outside harness/")

    # ---- 7: no adapters, no consumer-port duplicates
    if os.path.isdir(os.path.join(SERVER, "harness", "adapters")):
        errors.append("server/harness/adapters/ still exists (duplicate consumer ports are retired)")
    for path in walk(SERVER, (".hpp",)):
        text = read(path)
        if re.search(r'Consumer port\s*[—-]', text) and "/boundary/" in path.replace("\\", "/"):
            errors.append(f"{os.path.relpath(path, SERVER)}: still documented as a consumer-port duplicate")

    # ---- 8: pins forwarders (one include of a layer boundary / task-sheet header, no body)
    FORWARD_RE = re.compile(r'^\s*#\s*include\s*"((?:L[1-5]_\w+/boundary/|F_platform/tasks/)[^"]+)"', re.M)
    for path in walk(pins, (".hpp",)):
        text = read(path)
        targets = FORWARD_RE.findall(text)
        if not targets:
            continue
        body = [l for l in text.splitlines()
                if l.strip() and not l.strip().startswith(("#", "*", "/*", "//"))]
        if not body:
            errors.append(f"harness/pins/{os.path.basename(path)}: forwarder for {targets[0]} (include the defs/ harness header or the boundary header directly)")
        else:
            errors.append(f"harness/pins/{os.path.basename(path)}: includes layer header {targets[0]} (pins hold PODs/init contracts only)")

    for e in errors:
        print("  " + e)
    print(f"\n{len(errors)} error(s); {len(homes)} boundary interfaces, {len(connect_decls)} harness connections.")
    return 1 if (strict and errors) else 0


if __name__ == "__main__":
    sys.exit(main())
