#!/usr/bin/env python3
"""
check_harness_ports.py -- Ports & Adapters refactoring progress validator.

Parses HARNESS and ADAPTER tags from harness_wiring_map.hpp and verifies that
each Ports & Adapters entry has:
  1. A consumer port interface file in the consumer's boundary/ directory
  2. An adapter file in harness/adapters/
  3. The adapt function defined in harness_wiring_map.cpp

Usage:
    python scripts/check_harness_ports.py           # report status
    python scripts/check_harness_ports.py --strict   # exit 1 on errors (CI mode)
"""

import os
import re
import sys

# ── Tag patterns ──────────────────────────────────────────────────────────────

# HARNESS: adaptSPIBus  Board::ISPIBus  ->  Drivers::ISPIBus  [SPIAdapter, TracedSPIBus]
HARNESS_RE = re.compile(
    r'//\s*HARNESS:\s+'
    r'(\w+)\s+'                             # function name
    r'(\w+)::(\w+)\s+'                      # provider namespace::type
    r'->\s+'
    r'(\w+)::(\w+)\s+'                      # consumer namespace::type
    r'\[(\w+),\s*(\w+)\]'                   # [Adapter, Tap]
)

# ADAPTER: spi_adapter.hpp  SPIAdapter  [Board::ISPIBus -> Drivers::ISPIBus]
ADAPTER_RE = re.compile(
    r'//\s*ADAPTER:\s+'
    r'(\S+)\s+'                             # filename
    r'(\w+)\s+'                             # adapter class name
    r'\[(\w+)::(\w+)\s+->\s+(\w+)::(\w+)\]' # [From::Type -> To::Type]
)

# ── Namespace -> directory mapping ────────────────────────────────────────────

NAMESPACE_DIR = {
    "Board":     "layers/L5_board/boundary",
    "Drivers":   "layers/L4_drivers/boundary",
    "Services":  "layers/L3_services/boundary",
    "Protocol":  "layers/L2_protocol/boundary",
    "Transport": "layers/L1_transport/boundary",
    "Scheduler": "foundation/F_platform/tasks/boundary",
    "Platform":  "foundation/F_platform/boundary",
}

# ── Helpers ───────────────────────────────────────────────────────────────────


def camel_to_snake(name):
    """Convert CamelCase type name to snake_case filename.

    Handles acronym runs (consecutive uppercase) correctly:
      - Insert '_' before uppercase that follows lowercase  (e.g. Motor|Driver)
      - Insert '_' before uppercase-lowercase pair that follows uppercase
        (e.g. SPI|Bus -> the B starts a new word)

    Examples:
        IMotorDriver  -> imotor_driver
        ISPIBus       -> ispi_bus
        IEncoder      -> iencoder
        IByteChannel  -> ibyte_channel
    """
    result = []
    for i, ch in enumerate(name):
        if ch.isupper() and i > 0:
            prev_upper = name[i - 1].isupper()
            next_lower = (i + 1 < len(name) and name[i + 1].islower())
            # Case 1: lowercase -> uppercase  (e.g. motor|Driver)
            if name[i - 1].islower():
                result.append('_')
            # Case 2: uppercase run ending  (e.g. SPI|Bus — B starts new word)
            # Only when run is 2+ chars (skip single-char 'I' interface prefix)
            elif prev_upper and next_lower and i >= 2 and name[i - 2].isupper():
                result.append('_')
        result.append(ch.lower())
    return ''.join(result)


def type_to_filename(type_name):
    """Derive .hpp filename from an interface type name.

    Services::IMotorDriver -> imotor_driver.hpp
    """
    return camel_to_snake(type_name) + ".hpp"


def parse_harness_tags(filepath):
    """Parse HARNESS: tags from the wiring map header.

    Returns list of dicts with keys:
        func, provider_ns, provider_type, consumer_ns, consumer_type,
        adapter_class, tap_class
    """
    entries = []
    try:
        with open(filepath, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                m = HARNESS_RE.search(line)
                if m:
                    entries.append({
                        "func":          m.group(1),
                        "provider_ns":   m.group(2),
                        "provider_type": m.group(3),
                        "consumer_ns":   m.group(4),
                        "consumer_type": m.group(5),
                        "adapter_class": m.group(6),
                        "tap_class":     m.group(7),
                    })
    except (IOError, OSError) as e:
        print(f"ERROR: cannot read {filepath}: {e}")
        sys.exit(1)
    return entries


def parse_adapter_tags(filepath):
    """Parse ADAPTER: tags from the wiring map header.

    Returns list of dicts with keys:
        filename, adapter_class, from_ns, from_type, to_ns, to_type
    """
    entries = []
    try:
        with open(filepath, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                m = ADAPTER_RE.search(line)
                if m:
                    entries.append({
                        "filename":      m.group(1),
                        "adapter_class": m.group(2),
                        "from_ns":       m.group(3),
                        "from_type":     m.group(4),
                        "to_ns":         m.group(5),
                        "to_type":       m.group(6),
                    })
    except (IOError, OSError) as e:
        print(f"ERROR: cannot read {filepath}: {e}")
        sys.exit(1)
    return entries


def find_adapt_functions(filepath):
    """Scan harness_wiring_map.cpp for adapt function definitions.

    Returns set of function names found (e.g. {'adaptSPIBus', 'adaptMotorDriver'}).
    """
    # Match patterns like:  Type& adaptFoo(  or  Type& adaptFoo (
    func_re = re.compile(r'\b(\w+&)\s+(adapt\w+)\s*\(')
    found = set()
    try:
        with open(filepath, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                m = func_re.search(line)
                if m:
                    found.add(m.group(2))
    except (IOError, OSError):
        pass
    return found


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    strict = "--strict" in sys.argv

    # Locate project directories
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    server_root = os.path.join(project_root, "server")

    if not os.path.isdir(server_root):
        print(f"ERROR: server/ directory not found at {server_root}")
        sys.exit(1)

    wiring_hpp = os.path.join(server_root, "harness", "harness_wiring_map.hpp")
    wiring_cpp = os.path.join(server_root, "harness", "harness_wiring_map.cpp")
    adapters_dir = os.path.join(server_root, "harness", "adapters")

    if not os.path.isfile(wiring_hpp):
        print(f"ERROR: harness_wiring_map.hpp not found at {wiring_hpp}")
        sys.exit(1)

    # Parse tags
    harness_entries = parse_harness_tags(wiring_hpp)
    adapter_tags = parse_adapter_tags(wiring_hpp)
    adapt_functions = find_adapt_functions(wiring_cpp)

    if not harness_entries:
        print("No HARNESS tags found in harness_wiring_map.hpp.")
        print("Nothing to validate.")
        return

    # Build adapter tag lookup by adapter class name
    adapter_by_class = {}
    for a in adapter_tags:
        adapter_by_class[a["adapter_class"]] = a

    # Validate each HARNESS entry
    errors = []
    results = []

    for entry in harness_entries:
        func = entry["func"]
        consumer_ns = entry["consumer_ns"]
        consumer_type = entry["consumer_type"]
        adapter_class = entry["adapter_class"]
        checks = {}

        # 1. Consumer port interface file exists
        consumer_filename = type_to_filename(consumer_type)
        boundary_dir = NAMESPACE_DIR.get(consumer_ns)
        if boundary_dir:
            consumer_port_path = os.path.join(server_root, boundary_dir, consumer_filename)
            if os.path.isfile(consumer_port_path):
                checks["consumer_port"] = ("OK", consumer_port_path)
            else:
                rel = os.path.join(boundary_dir, consumer_filename).replace("\\", "/")
                checks["consumer_port"] = ("MISSING", rel)
                errors.append(f"{func}: consumer port missing: {rel}")
        else:
            checks["consumer_port"] = ("UNKNOWN_NS", consumer_ns)
            errors.append(f"{func}: unknown consumer namespace '{consumer_ns}'")

        # 2. Adapter file exists in harness/adapters/
        adapter_tag = adapter_by_class.get(adapter_class)
        if adapter_tag:
            adapter_filename = adapter_tag["filename"]
            adapter_path = os.path.join(adapters_dir, adapter_filename)
            if os.path.isfile(adapter_path):
                checks["adapter_file"] = ("OK", adapter_filename)
            else:
                checks["adapter_file"] = ("MISSING", f"harness/adapters/{adapter_filename}")
                errors.append(f"{func}: adapter file missing: harness/adapters/{adapter_filename}")
        else:
            # No ADAPTER tag -- derive filename from adapter class name
            adapter_filename = camel_to_snake(adapter_class) + ".hpp"
            adapter_path = os.path.join(adapters_dir, adapter_filename)
            if os.path.isfile(adapter_path):
                checks["adapter_file"] = ("OK", adapter_filename + " (no ADAPTER tag)")
            else:
                checks["adapter_file"] = ("MISSING", f"harness/adapters/{adapter_filename}")
                errors.append(f"{func}: adapter file missing: harness/adapters/{adapter_filename}")

        # 3. Adapt function exists in harness_wiring_map.cpp
        if func in adapt_functions:
            checks["adapt_function"] = ("OK", func)
        else:
            checks["adapt_function"] = ("MISSING", func)
            errors.append(f"{func}: adapt function not found in harness_wiring_map.cpp")

        results.append((entry, checks))

    # ── Report ────────────────────────────────────────────────────────────────

    print("=" * 70)
    print("Ports & Adapters Validation Report")
    print("=" * 70)
    print()

    for entry, checks in results:
        func = entry["func"]
        provider = f"{entry['provider_ns']}::{entry['provider_type']}"
        consumer = f"{entry['consumer_ns']}::{entry['consumer_type']}"
        all_ok = all(status == "OK" for status, _ in checks.values())
        marker = "PASS" if all_ok else "FAIL"

        print(f"[{marker}] {func}")
        print(f"       {provider}  ->  {consumer}")
        print(f"       Adapter: {entry['adapter_class']},  Tap: {entry['tap_class']}")

        for check_name, (status, detail) in checks.items():
            label = check_name.replace("_", " ").title()
            if status == "OK":
                print(f"         {label}: OK  ({detail})")
            else:
                print(f"         {label}: {status}  ({detail})")
        print()

    # ── Summary ───────────────────────────────────────────────────────────────

    total = len(harness_entries)
    passed = sum(1 for _, checks in results
                 if all(s == "OK" for s, _ in checks.values()))

    print("-" * 70)
    print(f"HARNESS entries:  {total}")
    print(f"  Fully wired:    {passed}")
    print(f"  With errors:    {total - passed}")
    if errors:
        print()
        print(f"Errors ({len(errors)}):")
        for e in errors:
            print(f"  - {e}")
    print()

    if strict and errors:
        print("STRICT MODE: exiting with status 1 due to errors.")
        sys.exit(1)

    if not errors:
        print("All Ports & Adapters connections validated successfully.")


if __name__ == "__main__":
    main()
