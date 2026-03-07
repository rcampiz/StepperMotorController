#!/usr/bin/env python3
"""
check_interface_ownership.py -- Interface ownership enforcement for STM32 firmware.

Reports abstract interfaces (pure virtual classes) defined in harness/pins/ that
should be owned by their providing namespace instead. Also tracks which interfaces
have already migrated to <layer>/boundary/ directories.

Per CODE_PHILOSOPHY.md Section 14 "Interface Ownership":
  Interfaces belong to the namespace that provides them -- not to the harness.
  Each namespace defines its boundary interfaces in <layer>/boundary/.
  Harness retains signal types (shared structs/enums), init contracts, and connectors.

Usage:
    python scripts/check_interface_ownership.py          # report migration status
    python scripts/check_interface_ownership.py --strict  # exit nonzero if unmigrated

The script detects abstract interfaces by scanning for classes/structs containing
pure virtual method declarations (= 0).
"""

import os
import re
import sys
from collections import defaultdict

# ── Patterns ─────────────────────────────────────────────────────────────────

# Match class/struct declarations (captures name)
CLASS_RE = re.compile(r'^\s*(?:class|struct)\s+(\w+)\b')

# Match pure virtual method declarations: ) = 0; or ) const = 0;
# Must follow a closing paren (with optional const/noexcept) to avoid matching
# member initializers like "int x = 0;"
PURE_VIRTUAL_RE = re.compile(r'\)\s*(?:const\s*)?(?:noexcept\s*)?=\s*0\s*;')

# Match override (concrete implementation of virtual) -- excludes from "has pure virtual"
OVERRIDE_RE = re.compile(r'\boverride\b')

# Match namespace declarations
NAMESPACE_RE = re.compile(r'^\s*namespace\s+(\w+)\s*\{')

# ── Provider mapping ─────────────────────────────────────────────────────────
# Maps interface names to their providing namespace/layer.
# Built from the codebase survey: who IMPLEMENTS each interface?

PROVIDER_MAP = {
    # Board:: (L5) provides
    "IGPIO":               ("Board",     "L5_board"),
    "ISPIBus":             ("Board",     "L5_board"),
    "ITimerCounter":       ("Board",     "L5_board"),
    "IMicrosecondClock":   ("Board",     "L5_board"),

    # Drivers:: (L4) provides
    "IMotorDriver":        ("Drivers",   "L4_drivers"),
    "IEncoderHardware":    ("Drivers",   "L4_drivers"),
    "ICanvas":             ("Drivers",   "L4_drivers"),
    "IJoystick":           ("Drivers",   "L4_drivers"),
    "IFlash":              ("Drivers",   "L4_drivers"),

    # Scheduler:: provides (encoder task exposes IEncoder)
    "IEncoder":            ("Scheduler", "F_platform/tasks"),

    # Board:: (L5) provides (encoder sampler is L5 DMA hardware)
    "IEncoderSampler":     ("Board",     "L5_board"),

    # Services:: (L3) provides
    "IMotorControlService":        ("Services",  "L3_services"),
    "IMotorEventReceiver":         ("Services",  "L3_services"),
    "IEncoderStatusSink":          ("Services",  "L3_services"),
    "IEncoderProcessing":          ("Services",  "L3_services"),
    "IFlashImageAccess":           ("Services",  "L3_services"),
    "IIndicatorRenderer":          ("Services",  "L3_services"),
    "ICommandDispatcher":          ("Services",  "L3_services"),
    "IMotionDispatcher":           ("Services",  "L3_services"),
    "IMotorConfigDispatcher":      ("Services",  "L3_services"),
    "ISafetyDispatcher":           ("Services",  "L3_services"),
    "IQueueDispatcher":            ("Services",  "L3_services"),
    "IControlModeDispatcher":      ("Services",  "L3_services"),
    "ISupervisorTrimDispatcher":   ("Services",  "L3_services"),
    "IEncoderDispatcher":          ("Services",  "L3_services"),
    "IMotorDriverDispatcher":      ("Services",  "L3_services"),
    "IDisplayDispatcher":          ("Services",  "L3_services"),
    "IFlashImageDispatcher":       ("Services",  "L3_services"),
    "ISystemDispatcher":           ("Services",  "L3_services"),
    "ITraceDispatcher":            ("Services",  "L3_services"),

    # Protocol:: (L2) provides
    "ICommandProcessor":   ("Protocol",  "L2_protocol"),

    # Transport:: (L1) provides
    "ITransport":          ("Transport", "L1_transport"),
    "IByteChannel":        ("Transport", "L1_transport"),

    # Platform:: (F_platform) provides
    "IClock":              ("Platform",  "F_platform"),
    "ILock":               ("Platform",  "F_platform"),
    "IQueue":              ("Platform",  "F_platform"),
    "ICriticalSection":    ("Platform",  "F_platform"),
    "IScheduler":          ("Platform",  "F_platform"),
    "ITaskStats":          ("Platform",  "F_platform"),

    # Scheduler:: (F_platform/tasks) provides
    "IMotorCommandSink":   ("Scheduler", "F_platform/tasks"),
    "IMotorTaskControl":   ("Scheduler", "F_platform/tasks"),
    "IRemoteDisplay":      ("Scheduler", "F_platform/tasks"),
    "IEmergencyStop":      ("Scheduler", "F_platform/tasks"),
    "ISafetyActions":      ("Scheduler", "F_platform/tasks"),
    "IEncoderFilterControl": ("Scheduler", "F_platform/tasks"),
    "IDispatchStats":      ("Scheduler", "F_platform/tasks"),
    "ISnapshotWriter":     ("Scheduler", "F_platform/tasks"),
    "ITelemetry":          ("Scheduler", "F_platform/tasks"),
    "IUIModeProvider":     ("Scheduler", "F_platform/tasks"),
    "IDebugCommands":      ("Scheduler", "F_platform/tasks"),
    "ITraceSink":          ("Services",  "L3_services"),
}


def find_abstract_interfaces(filepath):
    """Scan a file for abstract interface definitions (classes with = 0 methods).

    Returns list of (class_name, namespace) tuples.
    """
    interfaces = []
    current_namespace = None
    current_class = None
    brace_depth = 0
    class_brace_depth = 0
    has_pure_virtual = False
    has_override = False

    try:
        with open(filepath, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                # Track namespace
                ns_match = NAMESPACE_RE.match(line)
                if ns_match and current_class is None:
                    current_namespace = ns_match.group(1)

                # Track class/struct declarations
                cls_match = CLASS_RE.match(line)
                if cls_match and current_class is None:
                    name = cls_match.group(1)
                    # Skip forward declarations (no opening brace on same/next lines)
                    if "{" in line or ":" in line:
                        current_class = name
                        class_brace_depth = brace_depth
                        has_pure_virtual = False
                        has_override = False

                # Track brace depth
                brace_depth += line.count("{") - line.count("}")

                # Check for pure virtual methods inside a class
                if current_class and PURE_VIRTUAL_RE.search(line):
                    # Only count as pure virtual if not also an override
                    # (override = concrete implementation, not an abstract definition)
                    if not OVERRIDE_RE.search(line):
                        has_pure_virtual = True

                # Check for override methods (marks class as concrete, not abstract)
                if current_class and OVERRIDE_RE.search(line):
                    has_override = True

                # Class ended
                if current_class and brace_depth <= class_brace_depth:
                    # Only report as abstract interface if it has pure virtuals
                    # and is NOT a concrete class with overrides
                    if has_pure_virtual and not has_override:
                        interfaces.append((current_class, current_namespace))
                    current_class = None
                    has_pure_virtual = False
                    has_override = False

    except (IOError, OSError):
        pass

    return interfaces


def find_migrated_interfaces(server_root):
    """Find interfaces already migrated to <layer>/boundary/ directories."""
    migrated = {}
    layers_dir = os.path.join(server_root, "layers")
    foundation_dir = os.path.join(server_root, "foundation")

    for base_dir in [layers_dir, foundation_dir]:
        if not os.path.isdir(base_dir):
            continue
        for dirpath, dirnames, filenames in os.walk(base_dir):
            rel = os.path.relpath(dirpath, server_root).replace("\\", "/")
            if "/boundary" not in rel:
                continue
            for fname in filenames:
                if not fname.endswith(".hpp"):
                    continue
                fpath = os.path.join(dirpath, fname)
                for iface_name, ns in find_abstract_interfaces(fpath):
                    migrated[iface_name] = rel + "/" + fname

    return migrated


def main():
    strict = "--strict" in sys.argv

    # Find server root
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    server_root = os.path.join(project_root, "server")

    if not os.path.isdir(server_root):
        print(f"ERROR: server/ directory not found at {server_root}")
        sys.exit(1)

    harness_pins = os.path.join(server_root, "harness", "pins")
    if not os.path.isdir(harness_pins):
        print(f"ERROR: harness/pins/ directory not found at {harness_pins}")
        sys.exit(1)

    # Find migrated interfaces
    migrated = find_migrated_interfaces(server_root)

    # Scan harness/pins/ for abstract interfaces
    unmigrated = []
    by_provider = defaultdict(list)

    for fname in sorted(os.listdir(harness_pins)):
        if not fname.endswith(".hpp"):
            continue
        fpath = os.path.join(harness_pins, fname)
        interfaces = find_abstract_interfaces(fpath)

        for iface_name, ns in interfaces:
            if iface_name in migrated:
                continue  # already migrated

            provider_ns, provider_dir = PROVIDER_MAP.get(iface_name, ("Unknown", "unknown"))
            unmigrated.append((fname, iface_name, provider_ns, provider_dir))
            by_provider[provider_ns].append((iface_name, fname))

    # Report
    print("=" * 60)
    print("Interface Ownership Report")
    print("=" * 60)
    print()

    if migrated:
        print(f"Migrated ({len(migrated)} interfaces in boundary/ directories):")
        for name, path in sorted(migrated.items()):
            print(f"  {name:40s} -> {path}")
        print()

    if unmigrated:
        print(f"Unmigrated ({len(unmigrated)} abstract interfaces in harness/pins/):")
        print()
        for provider_ns in sorted(by_provider.keys()):
            ifaces = by_provider[provider_ns]
            layer_dir = PROVIDER_MAP.get(ifaces[0][0], ("", "unknown"))[1]
            print(f"  {provider_ns}:: ({layer_dir}/boundary/)  [{len(ifaces)} interfaces]")
            for iface_name, fname in sorted(ifaces):
                print(f"    {iface_name:40s} (harness/pins/{fname})")
            print()
    else:
        print("All abstract interfaces have migrated to their owning namespaces.")
        print()

    # Summary
    total = len(unmigrated) + len(migrated)
    print("-" * 60)
    print(f"Total abstract interfaces:  {total}")
    print(f"  Migrated to boundary/:    {len(migrated)}")
    print(f"  Still in harness/pins/:   {len(unmigrated)}")
    if total > 0:
        pct = len(migrated) / total * 100
        print(f"  Migration progress:       {pct:.0f}%")
    print()

    if strict and unmigrated:
        sys.exit(1)


if __name__ == "__main__":
    main()
