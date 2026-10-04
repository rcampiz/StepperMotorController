#!/usr/bin/env python3
"""
Complete Interface Ownership migration: Harness:: -> Provider:: namespace.

Converts remaining harness/pins connectors from full Harness:: definitions
to thin redirects, and updates all code references to use provider namespaces.

Usage:
    python scripts/migrate_harness_to_provider.py [--dry-run] [--group GROUP]

Groups: scheduler, services, transport, protocol, all (default)
"""

import os
import re
import sys
import argparse

SERVER_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "server")
PINS_DIR = os.path.join(SERVER_DIR, "harness", "pins")

# ============================================================================
# INTERFACE MAPPING: connector -> provider boundary + namespace + types
# ============================================================================

INTERFACES = {
    # ── Scheduler:: (12 interfaces) ── F_platform/tasks/boundary/ ──
    "iencoder.hpp": {
        "provider_path": "F_platform/tasks/boundary/iencoder.hpp",
        "provider_ns": "Scheduler",
        "names": ["IEncoder", "EncoderSnapshot"],
        "group": "scheduler",
    },
    "iencoder_filter_control.hpp": {
        "provider_path": "F_platform/tasks/boundary/iencoder_filter_control.hpp",
        "provider_ns": "Scheduler",
        "names": ["IEncoderFilterControl"],
        "group": "scheduler",
    },
    "iemergency_stop.hpp": {
        "provider_path": "F_platform/tasks/boundary/iemergency_stop.hpp",
        "provider_ns": "Scheduler",
        "names": ["IEmergencyStop"],
        "group": "scheduler",
    },
    "isafety_actions.hpp": {
        "provider_path": "F_platform/tasks/boundary/isafety_actions.hpp",
        "provider_ns": "Scheduler",
        "names": ["ISafetyActions", "g_safetyActions"],
        "group": "scheduler",
    },
    "imotor_command_sink.hpp": {
        "provider_path": "F_platform/tasks/boundary/imotor_command_sink.hpp",
        "provider_ns": "Scheduler",
        "names": ["IMotorCommandSink", "MotorCommand", "MotorCmdType", "g_motorCommandSink"],
        "group": "scheduler",
    },
    "imotor_task_control.hpp": {
        "provider_path": "F_platform/tasks/boundary/imotor_task_control.hpp",
        "provider_ns": "Scheduler",
        "names": ["IMotorTaskControl"],
        "group": "scheduler",
    },
    "idebug_commands.hpp": {
        "provider_path": "F_platform/tasks/boundary/idebug_commands.hpp",
        "provider_ns": "Scheduler",
        "names": ["IDebugCommands"],
        "group": "scheduler",
    },
    "iremote_display.hpp": {
        "provider_path": "F_platform/tasks/boundary/iremote_display.hpp",
        "provider_ns": "Scheduler",
        "names": ["IRemoteDisplay"],
        "group": "scheduler",
    },
    "idispatch_stats.hpp": {
        "provider_path": "F_platform/tasks/boundary/idispatch_stats.hpp",
        "provider_ns": "Scheduler",
        "names": ["IDispatchStats", "DispatchStats", "setDispatchStatsProvider", "dispatchStats"],
        "group": "scheduler",
    },
    "isnapshot_writer.hpp": {
        "provider_path": "F_platform/tasks/boundary/isnapshot_writer.hpp",
        "provider_ns": "Scheduler",
        "names": ["ISnapshotWriter", "setSnapshotWriter", "snapshotWriter"],
        "group": "scheduler",
    },
    "itelemetry.hpp": {
        "provider_path": "F_platform/tasks/boundary/itelemetry.hpp",
        "provider_ns": "Scheduler",
        "names": ["ITelemetry", "setTelemetryProvider", "telemetry"],
        "group": "scheduler",
    },
    "iui_mode.hpp": {
        "provider_path": "F_platform/tasks/boundary/iui_mode.hpp",
        "provider_ns": "Scheduler",
        "names": ["IUIModeProvider", "setUIModeProvider", "uiMode"],
        "group": "scheduler",
    },

    # ── Services:: (20 interfaces) ── L3_services/boundary/ ──
    # 12 sub-dispatchers:
    "imotion_dispatcher.hpp": {
        "provider_path": "L3_services/boundary/imotion_dispatcher.hpp",
        "provider_ns": "Services",
        "names": ["IMotionDispatcher"],
        "group": "services",
    },
    "isafety_dispatcher.hpp": {
        "provider_path": "L3_services/boundary/isafety_dispatcher.hpp",
        "provider_ns": "Services",
        "names": ["ISafetyDispatcher", "HeartbeatStatus"],
        "group": "services",
    },
    "iqueue_dispatcher.hpp": {
        "provider_path": "L3_services/boundary/iqueue_dispatcher.hpp",
        "provider_ns": "Services",
        "names": ["IQueueDispatcher"],
        "group": "services",
    },
    "icontrol_mode_dispatcher.hpp": {
        "provider_path": "L3_services/boundary/icontrol_mode_dispatcher.hpp",
        "provider_ns": "Services",
        "names": ["IControlModeDispatcher"],
        "group": "services",
    },
    "isupervisor_trim_dispatcher.hpp": {
        "provider_path": "L3_services/boundary/isupervisor_trim_dispatcher.hpp",
        "provider_ns": "Services",
        "names": ["ISupervisorTrimDispatcher", "SysIdSampleData"],
        "group": "services",
    },
    "iencoder_dispatcher.hpp": {
        "provider_path": "L3_services/boundary/iencoder_dispatcher.hpp",
        "provider_ns": "Services",
        "names": ["IEncoderDispatcher"],
        "group": "services",
    },
    "imotor_driver_dispatcher.hpp": {
        "provider_path": "L3_services/boundary/imotor_driver_dispatcher.hpp",
        "provider_ns": "Services",
        "names": ["IMotorDriverDispatcher"],
        "group": "services",
    },
    "imotor_config_dispatcher.hpp": {
        "provider_path": "L3_services/boundary/imotor_config_dispatcher.hpp",
        "provider_ns": "Services",
        "names": ["IMotorConfigDispatcher"],
        "group": "services",
    },
    "idisplay_dispatcher.hpp": {
        "provider_path": "L3_services/boundary/idisplay_dispatcher.hpp",
        "provider_ns": "Services",
        "names": ["IDisplayDispatcher"],
        "group": "services",
    },
    "iflash_image_dispatcher.hpp": {
        "provider_path": "L3_services/boundary/iflash_image_dispatcher.hpp",
        "provider_ns": "Services",
        "names": ["IFlashImageDispatcher"],
        "group": "services",
    },
    "isystem_dispatcher.hpp": {
        "provider_path": "L3_services/boundary/isystem_dispatcher.hpp",
        "provider_ns": "Services",
        "names": ["ISystemDispatcher"],
        "group": "services",
    },
    "itrace_dispatcher.hpp": {
        "provider_path": "L3_services/boundary/itrace_dispatcher.hpp",
        "provider_ns": "Services",
        "names": ["ITraceDispatcher"],
        "group": "services",
    },
    # Composite:
    "icommand_dispatcher.hpp": {
        "provider_path": "L3_services/boundary/icommand_dispatcher.hpp",
        "provider_ns": "Services",
        "names": ["ICommandDispatcher"],
        "group": "services",
    },
    # 7 non-dispatcher Services:: interfaces:
    "iencoder_processing.hpp": {
        "provider_path": "L3_services/boundary/iencoder_processing.hpp",
        "provider_ns": "Services",
        "names": ["IEncoderProcessing"],
        "group": "services",
    },
    "iencoder_status_sink.hpp": {
        "provider_path": "L3_services/boundary/iencoder_status_sink.hpp",
        "provider_ns": "Services",
        "names": ["IEncoderStatusSink", "EncoderTelemetryData"],
        "group": "services",
    },
    "iflash_image_access.hpp": {
        "provider_path": "L3_services/boundary/iflash_image_access.hpp",
        "provider_ns": "Services",
        "names": ["IFlashImageAccess"],
        "group": "services",
    },
    "iindicator_renderer.hpp": {
        "provider_path": "L3_services/boundary/iindicator_renderer.hpp",
        "provider_ns": "Services",
        "names": ["IIndicatorRenderer", "IndicatorParams"],
        "group": "services",
    },
    "imotor_control_service.hpp": {
        "provider_path": "L3_services/boundary/imotor_control_service.hpp",
        "provider_ns": "Services",
        "names": ["IMotorControlService"],
        "group": "services",
    },
    "imotor_event_receiver.hpp": {
        "provider_path": "L3_services/boundary/imotor_event_receiver.hpp",
        "provider_ns": "Services",
        "names": ["IMotorEventReceiver"],
        "group": "services",
    },
    "itrace_sink.hpp": {
        "provider_path": "L3_services/boundary/itrace_sink.hpp",
        "provider_ns": "Services",
        "names": ["ITraceSink"],
        "group": "services",
    },

    # ── Transport:: (2 interfaces) ── L1_transport/boundary/ ──
    "itransport.hpp": {
        "provider_path": "L1_transport/boundary/itransport.hpp",
        "provider_ns": "Transport",
        "names": ["ITransport"],
        "group": "transport",
    },
    "ibyte_channel.hpp": {
        "provider_path": "L1_transport/boundary/ibyte_channel.hpp",
        "provider_ns": "Transport",
        "names": ["IByteChannel"],
        "group": "transport",
    },

    # ── Protocol:: (1 interface) ── L2_protocol/boundary/ ──
    "icommand_processor.hpp": {
        "provider_path": "L2_protocol/boundary/icommand_processor.hpp",
        "provider_ns": "Protocol",
        "names": ["ICommandProcessor"],
        "group": "protocol",
    },
}


def collect_source_files(root):
    """Collect all .hpp/.cpp files under root, excluding boundary/ directories."""
    files = []
    for dirpath, dirnames, filenames in os.walk(root):
        # Skip generated consumer port directories and adapter directories
        # BUT include harness/pins, harness/taps, harness/trace etc.
        for fn in filenames:
            if fn.endswith(('.hpp', '.cpp')):
                files.append(os.path.join(dirpath, fn))
    return files


def convert_connector(connector_path, info, dry_run=False):
    """Convert a connector from full Harness:: definition to thin redirect."""
    provider_path = info["provider_path"]
    new_content = (
        f'/**\n'
        f' * @file {os.path.basename(connector_path)}\n'
        f' * @brief Harness connector -- includes {info["provider_ns"]}:: boundary definition\n'
        f' */\n'
        f'\n'
        f'#pragma once\n'
        f'\n'
        f'#include "{provider_path}"\n'
    )

    if dry_run:
        print(f"  [DRY] Would convert: {connector_path}")
        return True

    with open(connector_path, 'w', newline='\n') as f:
        f.write(new_content)
    return True


def replace_in_file(filepath, replacements, dry_run=False):
    """Apply text replacements in a file. Returns True if any changes made."""
    try:
        with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
            content = f.read()
    except (IOError, OSError):
        return False

    original = content
    for old, new in replacements:
        content = content.replace(old, new)

    if content != original:
        if not dry_run:
            with open(filepath, 'w', encoding='utf-8', newline='\n') as f:
                f.write(content)
        return True
    return False


def build_replacements(interfaces_to_process):
    """Build the list of (old, new) text replacement pairs."""
    replacements = []

    for filename, info in interfaces_to_process.items():
        ns = info["provider_ns"]
        for name in info["names"]:
            # Harness::TypeName -> ProviderNS::TypeName
            replacements.append((f"Harness::{name}", f"{ns}::{name}"))

    # Sort by length (longest first) to avoid partial replacements
    replacements.sort(key=lambda x: len(x[0]), reverse=True)

    # Also handle forward declarations: namespace Harness { class ITransport; }
    # These need special handling — we'll do a regex pass later

    return replacements


def fix_forward_declarations(content, interfaces_to_process):
    """Fix forward declarations like 'namespace Harness { class ITransport; }'."""
    for filename, info in interfaces_to_process.items():
        ns = info["provider_ns"]
        for name in info["names"]:
            # Single-line forward declaration
            pattern = rf'namespace\s+Harness\s*\{{\s*class\s+{re.escape(name)}\s*;\s*\}}'
            replacement = f'namespace {ns} {{ class {name}; }}'
            content = re.sub(pattern, replacement, content)
    return content


def fix_namespace_blocks(content, interfaces_to_process):
    """Fix 'namespace Harness {' blocks that only contain types from one provider namespace.
    This is a best-effort heuristic — complex blocks may need manual fixing."""
    # We don't auto-fix namespace blocks — too risky. The text replacements
    # handle qualified references (Harness::X -> NS::X). Namespace blocks in
    # connector files are replaced entirely by the redirect.
    return content


def process_cpp_accessor_files(interfaces_to_process, all_replacements, dry_run=False):
    """Handle .cpp files in harness/pins/ that define global accessors.
    Only processes files whose corresponding .hpp interface is in the current group."""
    # Map .hpp -> .cpp for interfaces that have accessor .cpp files
    cpp_candidates = {}
    for filename, info in interfaces_to_process.items():
        cpp_name = filename.replace('.hpp', '.cpp')
        cpp_path = os.path.join(PINS_DIR, cpp_name)
        if os.path.exists(cpp_path):
            cpp_candidates[cpp_name] = info

    changed = 0
    for cpp_file, info in cpp_candidates.items():
        filepath = os.path.join(PINS_DIR, cpp_file)
        target_ns = info["provider_ns"]

        try:
            with open(filepath, 'r', encoding='utf-8') as f:
                content = f.read()
        except (IOError, OSError):
            continue

        original = content

        # Replace namespace Harness { ... } -> namespace TargetNS { ... }
        content = content.replace("namespace Harness {", f"namespace {target_ns} {{")
        content = content.replace("} // namespace Harness", f"}} // namespace {target_ns}")

        # Apply only the relevant text replacements (not blanket Harness::)
        for old, new in all_replacements:
            content = content.replace(old, new)

        # Update include from harness/pins/ to provider boundary
        hpp_name = cpp_file.replace('.cpp', '.hpp')
        old_include = f'#include "harness/pins/{hpp_name}"'
        new_include = f'#include "{info["provider_path"]}"'
        content = content.replace(old_include, new_include)

        if content != original:
            if not dry_run:
                with open(filepath, 'w', encoding='utf-8', newline='\n') as f:
                    f.write(content)
            changed += 1
            print(f"  Updated accessor .cpp: {cpp_file}")

    return changed


def main():
    parser = argparse.ArgumentParser(description="Migrate Harness:: to Provider:: namespaces")
    parser.add_argument("--dry-run", action="store_true", help="Preview changes without writing")
    parser.add_argument("--group", default="all",
                        choices=["scheduler", "services", "transport", "protocol", "all"],
                        help="Which group to process")
    args = parser.parse_args()

    # Filter interfaces by group
    if args.group == "all":
        to_process = INTERFACES
    else:
        to_process = {k: v for k, v in INTERFACES.items() if v["group"] == args.group}

    print(f"Migrating {len(to_process)} interfaces ({args.group})")
    print(f"{'DRY RUN - no files will be modified' if args.dry_run else 'LIVE - files will be modified'}")
    print()

    # Step 1: Build replacement table
    replacements = build_replacements(to_process)
    print(f"Built {len(replacements)} replacement rules:")
    for old, new in replacements[:5]:
        print(f"  {old} -> {new}")
    if len(replacements) > 5:
        print(f"  ... and {len(replacements) - 5} more")
    print()

    # Step 2: Convert connectors to redirects
    print("--- Converting connectors ---")
    connectors_converted = 0
    for filename, info in to_process.items():
        connector_path = os.path.join(PINS_DIR, filename)
        if not os.path.exists(connector_path):
            print(f"  WARNING: Connector not found: {connector_path}")
            continue

        # Check if already a redirect
        with open(connector_path, 'r', encoding='utf-8') as f:
            content = f.read()
        if f'#include "{info["provider_path"]}"' in content and "namespace Harness" not in content:
            print(f"  Already redirect: {filename}")
            continue

        if convert_connector(connector_path, info, args.dry_run):
            connectors_converted += 1
            print(f"  Converted: {filename} -> {info['provider_path']}")

    print(f"\nConnectors converted: {connectors_converted}")
    print()

    # Step 3: Apply text replacements across all source files
    print("--- Applying namespace replacements ---")
    source_files = collect_source_files(SERVER_DIR)
    files_changed = 0
    total_replacements = 0

    for filepath in source_files:
        # Skip the connector files themselves (already handled)
        if os.path.join("harness", "pins") in filepath:
            # Still apply replacements to .cpp files in pins/
            pass

        try:
            with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
                content = f.read()
        except (IOError, OSError):
            continue

        original = content

        # Apply text replacements
        for old, new in replacements:
            content = content.replace(old, new)

        # Fix forward declarations
        content = fix_forward_declarations(content, to_process)

        if content != original:
            if not args.dry_run:
                with open(filepath, 'w', encoding='utf-8', newline='\n') as f:
                    f.write(content)
            rel = os.path.relpath(filepath, SERVER_DIR)
            files_changed += 1

    print(f"Files with replacements: {files_changed}")
    print()

    # Step 4: Handle accessor .cpp files
    print("--- Updating accessor .cpp files ---")
    accessor_changes = process_cpp_accessor_files(to_process, replacements, args.dry_run)
    print(f"Accessor files updated: {accessor_changes}")
    print()

    # Summary
    print("=" * 60)
    print(f"Migration summary ({args.group}):")
    print(f"  Connectors converted: {connectors_converted}")
    print(f"  Source files updated:  {files_changed}")
    print(f"  Accessor .cpp files:  {accessor_changes}")
    if args.dry_run:
        print("  (DRY RUN - no files were actually modified)")


if __name__ == "__main__":
    main()
