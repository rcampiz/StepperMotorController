#!/usr/bin/env python3
"""
check_harness_wiring.py -- Harness wiring manifest enforcement.

Parses server/harness/harness_wiring_map.hpp for tagged entries and validates
that every documented file exists, every connector redirects to the correct
target, and no undocumented files lurk in the harness directories.

Usage:
    python scripts/check_harness_wiring.py          # report wiring status
    python scripts/check_harness_wiring.py --strict  # exit nonzero on errors
"""

import os
import re
import sys

# -- Tag patterns (parsed from harness_wiring_map.hpp) -------------------------

CONNECTOR_RE = re.compile(
    r'//\s*CONNECTOR:\s*(\S+)\s*->\s*(\S+)\s*\[(.+)\]')
REDIRECT_RE = re.compile(
    r'//\s*REDIRECT:\s*(\S+)\s*->\s*(\S+)\s*\[(.+)\]')
OWNED_RE = re.compile(
    r'//\s*OWNED:\s*(\S+)\s*\[(.+)\]')
INIT_RE = re.compile(
    r'//\s*INIT:\s*(\S+)\s*\[(.+)\]')
ACCESSOR_RE = re.compile(
    r'//\s*ACCESSOR:\s*(\S+)\s*\[(.+)\]')
TAP_RE = re.compile(
    r'//\s*TAP:\s*(\S+)\s+wraps\s+(\S+)\s*\[(.+)\]')
TRACE_RE = re.compile(
    r'//\s*TRACE:\s*(\S+)\s*\[(.+)\]')
TOPLEVEL_RE = re.compile(
    r'//\s*TOPLEVEL:\s*(\S+)\s*\[(.+)\]')
WIRING_RE = re.compile(
    r'//\s*WIRING:\s*(\S+)\s*\[(.+)\]')
ADAPT_RE = re.compile(
    r'//\s*ADAPT:\s*(\S+)\s*\[(.+)\]')
HARNESS_RE = re.compile(
    r'//\s*HARNESS:\s*(\S+)\s+(\S+)\s*->\s*(\S+)\s*\[(.+)\]')
ADAPTER_RE = re.compile(
    r'//\s*ADAPTER:\s*(\S+)\s+(\S+)\s*\[(.+)\]')


def parse_manifest(manifest_path):
    """Parse harness_wiring_map.hpp and return categorized entries."""
    connectors = []   # (filename, target_dir, description)
    redirects = []    # (filename, target_path, description)
    owned = []        # (filename, description)
    inits = []        # (filename, description)
    accessors = []    # (filename, description)
    taps = []         # (filename, wraps_interface, boundary_name)
    traces = []       # (filename, description)
    toplevels = []    # (filename, description)
    wirings = []      # (function_name, description)
    adapts = []       # (function_name, description)
    harnesses = []    # (func_name, provider_type, consumer_type, adapter_tap)
    adapters = []     # (filename, class_name, description)

    with open(manifest_path, "r", encoding="utf-8") as f:
        for line in f:
            m = CONNECTOR_RE.search(line)
            if m:
                connectors.append((m.group(1), m.group(2).rstrip("/"),
                                   m.group(3).strip()))
                continue

            m = REDIRECT_RE.search(line)
            if m:
                redirects.append((m.group(1), m.group(2).strip(),
                                  m.group(3).strip()))
                continue

            m = OWNED_RE.search(line)
            if m:
                owned.append((m.group(1), m.group(2).strip()))
                continue

            m = INIT_RE.search(line)
            if m:
                inits.append((m.group(1), m.group(2).strip()))
                continue

            m = ACCESSOR_RE.search(line)
            if m:
                accessors.append((m.group(1), m.group(2).strip()))
                continue

            m = TAP_RE.search(line)
            if m:
                taps.append((m.group(1), m.group(2).strip(),
                             m.group(3).strip()))
                continue

            m = TRACE_RE.search(line)
            if m:
                traces.append((m.group(1), m.group(2).strip()))
                continue

            m = TOPLEVEL_RE.search(line)
            if m:
                toplevels.append((m.group(1), m.group(2).strip()))
                continue

            m = WIRING_RE.search(line)
            if m:
                wirings.append((m.group(1), m.group(2).strip()))
                continue

            m = ADAPT_RE.search(line)
            if m:
                adapts.append((m.group(1), m.group(2).strip()))
                continue

            m = HARNESS_RE.search(line)
            if m:
                harnesses.append((m.group(1), m.group(2).strip(),
                                  m.group(3).strip(), m.group(4).strip()))
                continue

            m = ADAPTER_RE.search(line)
            if m:
                adapters.append((m.group(1), m.group(2).strip(),
                                 m.group(3).strip()))
                continue

    return (connectors, redirects, owned, inits, accessors,
            taps, traces, toplevels, wirings, adapts,
            harnesses, adapters)


def get_include_target(filepath):
    """Extract the #include "..." path from a file, if present."""
    try:
        with open(filepath, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                line = line.strip()
                if line.startswith("#include"):
                    m = re.match(r'#include\s+"([^"]+)"', line)
                    if m:
                        return m.group(1)
    except (IOError, OSError):
        pass
    return None


def list_files(directory):
    """List all .hpp and .cpp files in a directory (non-recursive)."""
    if not os.path.isdir(directory):
        return set()
    return {f for f in os.listdir(directory)
            if f.endswith(".hpp") or f.endswith(".cpp")}


def resolve_include_path(server_root, include_path):
    """Resolve a firmware include path to an actual filesystem path.

    Include paths like 'L5_board/boundary/igpio.hpp' live under server/layers/,
    while 'F_platform/boundary/iclock.hpp' lives under server/foundation/.
    """
    # L*_ prefixes live in server/layers/
    # F_ prefixes live in server/foundation/
    first_component = include_path.split("/")[0]
    if first_component.startswith("L"):
        return os.path.join(server_root, "layers", *include_path.split("/"))
    elif first_component.startswith("F_"):
        return os.path.join(server_root, "foundation", *include_path.split("/"))
    else:
        # Fallback: try directly under server/
        return os.path.join(server_root, *include_path.split("/"))


def main():
    strict = "--strict" in sys.argv

    # Find project paths
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    server_root = os.path.join(project_root, "server")
    harness_root = os.path.join(server_root, "harness")

    manifest_path = os.path.join(harness_root, "harness_wiring_map.hpp")
    if not os.path.isfile(manifest_path):
        print(f"ERROR: manifest not found at {manifest_path}")
        sys.exit(1)

    pins_dir = os.path.join(harness_root, "pins")
    taps_dir = os.path.join(harness_root, "taps")
    trace_dir = os.path.join(harness_root, "trace")

    adapters_dir = os.path.join(harness_root, "adapters")

    # Parse manifest
    (connectors, redirects, owned, inits,
     accessors, taps, traces, toplevels,
     wirings, adapts, harnesses, adapters) = parse_manifest(manifest_path)

    errors = []
    warnings = []

    # -- Validate connectors ---------------------------------------------------
    connector_ok = 0
    for filename, target_dir, desc in connectors:
        filepath = os.path.join(pins_dir, filename)
        if not os.path.isfile(filepath):
            errors.append(f"CONNECTOR missing: pins/{filename}")
            continue

        # Check redirect target
        expected_include = target_dir + "/" + filename
        actual_include = get_include_target(filepath)
        if actual_include is None:
            errors.append(f"CONNECTOR no #include: pins/{filename}")
            continue

        if actual_include != expected_include:
            errors.append(
                f"CONNECTOR mismatch: pins/{filename} "
                f"includes '{actual_include}', expected '{expected_include}'")
            continue

        # Verify target file exists
        target_file = resolve_include_path(server_root, expected_include)
        if not os.path.isfile(target_file):
            errors.append(
                f"CONNECTOR target missing: {expected_include}")
            continue

        connector_ok += 1

    # -- Validate redirects ----------------------------------------------------
    redirect_ok = 0
    for filename, target_path, desc in redirects:
        filepath = os.path.join(pins_dir, filename)
        if not os.path.isfile(filepath):
            errors.append(f"REDIRECT missing: pins/{filename}")
            continue

        actual_include = get_include_target(filepath)
        if actual_include is None:
            errors.append(f"REDIRECT no #include: pins/{filename}")
            continue

        if actual_include != target_path:
            errors.append(
                f"REDIRECT mismatch: pins/{filename} "
                f"includes '{actual_include}', expected '{target_path}'")
            continue

        target_file = resolve_include_path(server_root, target_path)
        if not os.path.isfile(target_file):
            errors.append(f"REDIRECT target missing: {target_path}")
            continue

        redirect_ok += 1

    # -- Validate owned files --------------------------------------------------
    owned_ok = 0
    for filename, desc in owned:
        filepath = os.path.join(pins_dir, filename)
        if not os.path.isfile(filepath):
            errors.append(f"OWNED missing: pins/{filename}")
            continue
        owned_ok += 1

    # -- Validate init contracts -----------------------------------------------
    init_ok = 0
    for filename, desc in inits:
        filepath = os.path.join(pins_dir, filename)
        if not os.path.isfile(filepath):
            errors.append(f"INIT missing: pins/{filename}")
            continue
        init_ok += 1

    # -- Validate accessors ----------------------------------------------------
    accessor_ok = 0
    for filename, desc in accessors:
        filepath = os.path.join(pins_dir, filename)
        if not os.path.isfile(filepath):
            errors.append(f"ACCESSOR missing: pins/{filename}")
            continue
        accessor_ok += 1

    # -- Validate taps ---------------------------------------------------------
    tap_ok = 0
    for filename, wraps, boundary in taps:
        filepath = os.path.join(taps_dir, filename)
        if not os.path.isfile(filepath):
            errors.append(f"TAP missing: taps/{filename}")
            continue
        tap_ok += 1

    # -- Validate trace files --------------------------------------------------
    trace_ok = 0
    for filename, desc in traces:
        filepath = os.path.join(trace_dir, filename)
        if not os.path.isfile(filepath):
            errors.append(f"TRACE missing: trace/{filename}")
            continue
        trace_ok += 1

    # -- Validate top-level files ----------------------------------------------
    toplevel_ok = 0
    for filename, desc in toplevels:
        filepath = os.path.join(harness_root, filename)
        if not os.path.isfile(filepath):
            errors.append(f"TOPLEVEL missing: harness/{filename}")
            continue
        toplevel_ok += 1

    # -- Validate wiring functions ---------------------------------------------
    wiring_ok = 0
    wiring_cpp_path = os.path.join(harness_root, "harness_wiring_map.cpp")
    system_init_path = os.path.join(server_root, "system_init.cpp")

    wiring_cpp = ""
    if os.path.isfile(wiring_cpp_path):
        with open(wiring_cpp_path, "r", encoding="utf-8") as f:
            wiring_cpp = f.read()
    else:
        errors.append("WIRING: harness_wiring_map.cpp not found")

    system_init = ""
    if os.path.isfile(system_init_path):
        with open(system_init_path, "r", encoding="utf-8") as f:
            system_init = f.read()
    else:
        errors.append("WIRING: system_init.cpp not found")

    for func_name, desc in wirings:
        defined = (f"void {func_name}(" in wiring_cpp or
                   f"bool {func_name}(" in wiring_cpp)
        called = f"{func_name}(" in system_init

        if not defined:
            errors.append(
                f"WIRING missing definition: {func_name}() "
                f"not in harness_wiring_map.cpp")
        if not called:
            errors.append(
                f"WIRING not called: {func_name}() not in system_init.cpp")

        if defined and called:
            wiring_ok += 1

    # Check for accessor calls that leaked into system_init.cpp
    ACCESSOR_CALLS = [
        "setMicrosecondClock(", "setTaskStatsProvider(",
        "setTelemetryProvider(", "setSnapshotWriter(",
        "setUIModeProvider(", "setDispatchStatsProvider(",
    ]
    for call in ACCESSOR_CALLS:
        if call in system_init:
            warnings.append(
                f"WIRING leak: {call} found in system_init.cpp "
                f"(should be in harness_wiring_map.cpp)")

    # -- Validate adapt functions ----------------------------------------------
    adapt_ok = 0
    for func_name, desc in adapts:
        # Return type varies, so just check for the function name with (
        defined = f" {func_name}(" in wiring_cpp
        if not defined:
            errors.append(
                f"ADAPT missing definition: {func_name}() "
                f"not in harness_wiring_map.cpp")
        else:
            adapt_ok += 1

    # -- Enforce no #ifdef ENABLE_INTERFACE_TRACE outside harness/ -------------
    ifdef_leaks = []
    for dirpath, dirnames, filenames in os.walk(server_root):
        # Skip harness/ directory (that's where they belong)
        rel = os.path.relpath(dirpath, server_root).replace("\\", "/")
        if rel.startswith("harness") or rel == "harness":
            continue
        for fname in filenames:
            if not fname.endswith(".cpp") and not fname.endswith(".hpp"):
                continue
            fpath = os.path.join(dirpath, fname)
            try:
                with open(fpath, "r", encoding="utf-8", errors="replace") as f:
                    for lineno, line in enumerate(f, 1):
                        if "#ifdef ENABLE_INTERFACE_TRACE" in line:
                            rel_file = os.path.relpath(fpath, server_root)
                            rel_file = rel_file.replace("\\", "/")
                            ifdef_leaks.append(f"{rel_file}:{lineno}")
            except (IOError, OSError):
                pass

    for leak in ifdef_leaks:
        errors.append(
            f"ADAPT leak: #ifdef ENABLE_INTERFACE_TRACE in {leak} "
            f"(should be in harness_wiring_map.cpp)")

    # -- Validate HARNESS connections ------------------------------------------
    harness_ok = 0
    for func_name, provider, consumer, adapter_tap in harnesses:
        defined = f" {func_name}(" in wiring_cpp
        if not defined:
            errors.append(
                f"HARNESS missing definition: {func_name}() "
                f"not in harness_wiring_map.cpp")
        else:
            harness_ok += 1

    # -- Validate ADAPTER files ------------------------------------------------
    adapter_ok = 0
    for filename, class_name, desc in adapters:
        filepath = os.path.join(adapters_dir, filename)
        if not os.path.isfile(filepath):
            errors.append(f"ADAPTER missing: adapters/{filename}")
        else:
            adapter_ok += 1

    # -- Orphan detection ------------------------------------------------------
    # Collect all documented filenames per directory
    pins_documented = set()
    for filename, _, _ in connectors:
        pins_documented.add(filename)
    for filename, _, _ in redirects:
        pins_documented.add(filename)
    for filename, _ in owned:
        pins_documented.add(filename)
    for filename, _ in inits:
        pins_documented.add(filename)
    for filename, _ in accessors:
        pins_documented.add(filename)

    taps_documented = {filename for filename, _, _ in taps}
    trace_documented = {filename for filename, _ in traces}
    toplevel_documented = {filename for filename, _ in toplevels}
    adapters_documented = {filename for filename, _, _ in adapters}

    pins_actual = list_files(pins_dir)
    taps_actual = list_files(taps_dir)
    trace_actual = list_files(trace_dir)
    adapters_actual = list_files(adapters_dir)
    toplevel_actual = {f for f in list_files(harness_root)
                       if f != "harness_wiring_map.hpp"}

    pins_orphans = sorted(pins_actual - pins_documented)
    taps_orphans = sorted(taps_actual - taps_documented)
    trace_orphans = sorted(trace_actual - trace_documented)
    adapters_orphans = sorted(adapters_actual - adapters_documented)
    toplevel_orphans = sorted(toplevel_actual - toplevel_documented)

    for f in pins_orphans:
        warnings.append(f"ORPHAN: pins/{f} (not in manifest)")
    for f in taps_orphans:
        warnings.append(f"ORPHAN: taps/{f} (not in manifest)")
    for f in trace_orphans:
        warnings.append(f"ORPHAN: trace/{f} (not in manifest)")
    for f in adapters_orphans:
        warnings.append(f"ORPHAN: adapters/{f} (not in manifest)")
    for f in toplevel_orphans:
        warnings.append(f"ORPHAN: harness/{f} (not in manifest)")

    # -- Report ----------------------------------------------------------------
    print("=" * 60)
    print("Harness Wiring Validation Report")
    print("=" * 60)
    print()

    print(f"Connectors ({connector_ok}/{len(connectors)} valid):")
    for filename, target_dir, desc in connectors:
        filepath = os.path.join(pins_dir, filename)
        ok = os.path.isfile(filepath) and not any(
            filename in e for e in errors if "CONNECTOR" in e)
        mark = "+" if ok else "X"
        print(f"  {mark} {filename:44s} -> {target_dir + '/' + filename}")
    print()

    print(f"Redirects ({redirect_ok}/{len(redirects)} valid):")
    for filename, target_path, desc in redirects:
        ok = not any(filename in e for e in errors if "REDIRECT" in e)
        mark = "+" if ok else "X"
        print(f"  {mark} {filename:44s} -> {target_path}")
    print()

    print(f"Owned files ({owned_ok}/{len(owned)} present):")
    for filename, desc in owned:
        ok = not any(filename in e for e in errors if "OWNED" in e)
        mark = "+" if ok else "X"
        print(f"  {mark} {filename}")
    print()

    print(f"Init contracts ({init_ok}/{len(inits)} present):")
    for filename, desc in inits:
        ok = not any(filename in e for e in errors if "INIT" in e)
        mark = "+" if ok else "X"
        print(f"  {mark} {filename}")
    print()

    print(f"Accessors ({accessor_ok}/{len(accessors)} present):")
    for filename, desc in accessors:
        ok = not any(filename in e for e in errors if "ACCESSOR" in e)
        mark = "+" if ok else "X"
        print(f"  {mark} {filename}")
    print()

    print(f"Taps ({tap_ok}/{len(taps)} present):")
    for filename, wraps, boundary in taps:
        ok = not any(filename in e for e in errors if "TAP" in e)
        mark = "+" if ok else "X"
        print(f"  {mark} {filename:44s} wraps {wraps}")
    print()

    print(f"Trace ({trace_ok}/{len(traces)} present):")
    for filename, desc in traces:
        ok = not any(filename in e for e in errors if "TRACE" in e)
        mark = "+" if ok else "X"
        print(f"  {mark} {filename}")
    print()

    print(f"Top-level ({toplevel_ok}/{len(toplevels)} present):")
    for filename, desc in toplevels:
        ok = not any(filename in e for e in errors if "TOPLEVEL" in e)
        mark = "+" if ok else "X"
        print(f"  {mark} {filename}")
    print()

    print(f"Wiring ({wiring_ok}/{len(wirings)} valid):")
    for func_name, desc in wirings:
        ok = not any(func_name in e for e in errors if "WIRING" in e)
        mark = "+" if ok else "X"
        print(f"  {mark} {func_name:36s} [{desc}]")
    wiring_leaks = [w for w in warnings if "WIRING leak" in w]
    if wiring_leaks:
        for w in wiring_leaks:
            print(f"  ! {w}")
    print()

    print(f"Legacy Adapters ({adapt_ok}/{len(adapts)} valid):")
    for func_name, desc in adapts:
        ok = not any(func_name in e for e in errors if "ADAPT" in e
                     and "missing" in e)
        mark = "+" if ok else "X"
        print(f"  {mark} {func_name:36s} [{desc}]")
    adapt_leak_list = [e for e in errors if "ADAPT leak" in e]
    if adapt_leak_list:
        for e in adapt_leak_list:
            print(f"  ! {e}")
    print()

    print(f"HARNESS connections ({harness_ok}/{len(harnesses)} valid):")
    for func_name, provider, consumer, adapter_tap in harnesses:
        ok = not any(func_name in e for e in errors if "HARNESS" in e)
        mark = "+" if ok else "X"
        print(f"  {mark} {func_name:24s} {provider:28s} -> {consumer}")
    print()

    print(f"ADAPTER files ({adapter_ok}/{len(adapters)} present):")
    for filename, class_name, desc in adapters:
        ok = not any(filename in e for e in errors if "ADAPTER" in e)
        mark = "+" if ok else "X"
        print(f"  {mark} {filename:36s} {class_name}")
    print()

    total_orphans = (len(pins_orphans) + len(taps_orphans) +
                     len(trace_orphans) + len(adapters_orphans) +
                     len(toplevel_orphans))
    print(f"Orphans ({total_orphans} undocumented files):")
    if total_orphans == 0:
        print("  (none)")
    else:
        for w in warnings:
            if "ORPHAN" in w:
                print(f"  ! {w}")
    print()

    # -- Summary ---------------------------------------------------------------
    total_entries = (len(connectors) + len(redirects) + len(owned) +
                     len(inits) + len(accessors) + len(taps) +
                     len(traces) + len(toplevels) + len(wirings) +
                     len(adapts) + len(harnesses) + len(adapters))
    total_ok = (connector_ok + redirect_ok + owned_ok + init_ok +
                accessor_ok + tap_ok + trace_ok + toplevel_ok + wiring_ok +
                adapt_ok + harness_ok + adapter_ok)

    print("-" * 60)
    print(f"Manifest entries:  {total_entries}")
    print(f"  Valid:           {total_ok}")
    print(f"  Errors:          {len(errors)}")
    print(f"  Orphans:         {total_orphans}")
    print()

    if errors:
        print("ERRORS:")
        for e in errors:
            print(f"  {e}")
        print()

    if not errors and total_orphans == 0:
        print("All harness files documented and valid.")
        print()

    if strict and (errors or total_orphans > 0):
        sys.exit(1)


if __name__ == "__main__":
    main()
