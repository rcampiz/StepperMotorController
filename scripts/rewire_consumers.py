#!/usr/bin/env python3
"""
rewire_consumers.py — Update harness connectors and consumer-side namespace references.

For each HARNESS interface:
  1. Update the harness/pins/ connector to redirect to consumer port
  2. Change provider-side files (taps) to use direct provider boundary includes
  3. Replace ProviderNS::TypeName with ConsumerNS::TypeName in consumer-layer files

Run from project root:
    python scripts/rewire_consumers.py [--dry-run] [--phase N]
"""

import os
import re
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
SERVER_ROOT = PROJECT_ROOT / "server"

# ── Interface mapping ────────────────────────────────────────────────────────
# provider_include, consumer_ns, consumer_boundary_dir, connector_file, class_name,
# associated_types, consumer_scope_dir

INTERFACES = [
    # Phase 2: Board:: → Drivers/Scheduler
    {
        "provider_inc": "L5_board/boundary/igpio.hpp",
        "provider_ns": "Board",
        "consumer_ns": "Drivers",
        "consumer_bdir": "L4_drivers/boundary",
        "connector": "igpio.hpp",
        "class_name": "IGPIO",
        "types": [],  # No associated types
        "scope": "layers/L4_drivers",  # Where to do namespace replacements
        "phase": 2,
    },
    {
        "provider_inc": "L5_board/boundary/itimer_counter.hpp",
        "provider_ns": "Board",
        "consumer_ns": "Scheduler",
        "consumer_bdir": "F_platform/tasks/boundary",
        "connector": "itimer_counter.hpp",
        "class_name": "ITimerCounter",
        "types": [],
        "scope": "foundation/F_platform/tasks",
        "phase": 2,
    },
    {
        "provider_inc": "L5_board/boundary/iencoder_sampler.hpp",
        "provider_ns": "Board",
        "consumer_ns": "Scheduler",
        "consumer_bdir": "F_platform/tasks/boundary",
        "connector": "iencoder_sampler.hpp",
        "class_name": "IEncoderSampler",
        "types": [],
        "scope": "foundation/F_platform/tasks",
        "phase": 2,
    },
    {
        "provider_inc": "L5_board/boundary/imicrosecond_clock.hpp",
        "provider_ns": "Board",
        "consumer_ns": "Scheduler",
        "consumer_bdir": "F_platform/tasks/boundary",
        "connector": "imicrosecond_clock.hpp",
        "class_name": "IMicrosecondClock",
        "types": [],
        "scope": "foundation/F_platform/tasks",
        "phase": 2,
    },

    # Phase 3: Drivers:: → Services/Scheduler
    {
        "provider_inc": "L4_drivers/boundary/icanvas.hpp",
        "provider_ns": "Drivers",
        "consumer_ns": "Services",
        "consumer_bdir": "L3_services/boundary",
        "connector": "icanvas.hpp",
        "class_name": "ICanvas",
        "types": [],  # Canvas:: constants are in a sub-namespace, not qualified with Drivers::
        "scope": "layers/L3_services",
        "phase": 3,
    },
    {
        "provider_inc": "L4_drivers/boundary/ijoystick.hpp",
        "provider_ns": "Drivers",
        "consumer_ns": "Services",
        "consumer_bdir": "L3_services/boundary",
        "connector": "ijoystick.hpp",
        "class_name": "IJoystick",
        "types": [],
        "scope": "layers/L3_services",
        "phase": 3,
    },
    {
        "provider_inc": "L4_drivers/boundary/iflash.hpp",
        "provider_ns": "Drivers",
        "consumer_ns": "Services",
        "consumer_bdir": "L3_services/boundary",
        "connector": "iflash.hpp",
        "class_name": "IFlash",
        "types": ["FlashJedecId"],
        "scope": "layers/L3_services",
        "phase": 3,
    },
    {
        "provider_inc": "L4_drivers/boundary/iencoder_hardware.hpp",
        "provider_ns": "Drivers",
        "consumer_ns": "Scheduler",
        "consumer_bdir": "F_platform/tasks/boundary",
        "connector": "iencoder_hardware.hpp",
        "class_name": "IEncoderHardware",
        "types": [],
        "scope": "foundation/F_platform/tasks",
        "phase": 3,
    },

    # Phase 4: Scheduler:: → Services
    {
        "provider_inc": "F_platform/tasks/boundary/iencoder_filter_control.hpp",
        "provider_ns": "Scheduler",
        "consumer_ns": "Services",
        "consumer_bdir": "L3_services/boundary",
        "connector": "iencoder_filter_control.hpp",
        "class_name": "IEncoderFilterControl",
        "types": [],
        "scope": "layers/L3_services",
        "phase": 4,
    },
    {
        "provider_inc": "F_platform/tasks/boundary/iemergency_stop.hpp",
        "provider_ns": "Scheduler",
        "consumer_ns": "Services",
        "consumer_bdir": "L3_services/boundary",
        "connector": "iemergency_stop.hpp",
        "class_name": "IEmergencyStop",
        "types": [],
        "scope": "layers/L3_services",
        "phase": 4,
    },
    {
        "provider_inc": "F_platform/tasks/boundary/isafety_actions.hpp",
        "provider_ns": "Scheduler",
        "consumer_ns": "Services",
        "consumer_bdir": "L3_services/boundary",
        "connector": "isafety_actions.hpp",
        "class_name": "ISafetyActions",
        "types": [],
        "scope": "layers/L3_services",
        "phase": 4,
    },
    {
        "provider_inc": "F_platform/tasks/boundary/imotor_command_sink.hpp",
        "provider_ns": "Scheduler",
        "consumer_ns": "Services",
        "consumer_bdir": "L3_services/boundary",
        "connector": "imotor_command_sink.hpp",
        "class_name": "IMotorCommandSink",
        "types": ["MotorCmdType", "MotorCommand"],
        "scope": "layers/L3_services",
        "phase": 4,
    },
    {
        "provider_inc": "F_platform/tasks/boundary/imotor_task_control.hpp",
        "provider_ns": "Scheduler",
        "consumer_ns": "Services",
        "consumer_bdir": "L3_services/boundary",
        "connector": "imotor_task_control.hpp",
        "class_name": "IMotorTaskControl",
        "types": [],
        "scope": "layers/L3_services",
        "phase": 4,
    },
    {
        "provider_inc": "F_platform/tasks/boundary/idebug_commands.hpp",
        "provider_ns": "Scheduler",
        "consumer_ns": "Services",
        "consumer_bdir": "L3_services/boundary",
        "connector": "idebug_commands.hpp",
        "class_name": "IDebugCommands",
        "types": [],
        "scope": "layers/L3_services",
        "phase": 4,
    },
    {
        "provider_inc": "F_platform/tasks/boundary/iremote_display.hpp",
        "provider_ns": "Scheduler",
        "consumer_ns": "Services",
        "consumer_bdir": "L3_services/boundary",
        "connector": "iremote_display.hpp",
        "class_name": "IRemoteDisplay",
        "types": [],
        "scope": "layers/L3_services",
        "phase": 4,
    },
    {
        "provider_inc": "F_platform/tasks/boundary/idispatch_stats.hpp",
        "provider_ns": "Scheduler",
        "consumer_ns": "Services",
        "consumer_bdir": "L3_services/boundary",
        "connector": "idispatch_stats.hpp",
        "class_name": "IDispatchStats",
        "types": ["DispatchStats"],
        "scope": "layers/L3_services",
        "phase": 4,
    },
    {
        "provider_inc": "F_platform/tasks/boundary/isnapshot_writer.hpp",
        "provider_ns": "Scheduler",
        "consumer_ns": "Services",
        "consumer_bdir": "L3_services/boundary",
        "connector": "isnapshot_writer.hpp",
        "class_name": "ISnapshotWriter",
        "types": [],
        "scope": "layers/L3_services",
        "phase": 4,
    },
    {
        "provider_inc": "F_platform/tasks/boundary/itelemetry.hpp",
        "provider_ns": "Scheduler",
        "consumer_ns": "Services",
        "consumer_bdir": "L3_services/boundary",
        "connector": "itelemetry.hpp",
        "class_name": "ITelemetry",
        "types": [],
        "scope": "layers/L3_services",
        "phase": 4,
    },
    {
        "provider_inc": "F_platform/tasks/boundary/iui_mode.hpp",
        "provider_ns": "Scheduler",
        "consumer_ns": "Services",
        "consumer_bdir": "L3_services/boundary",
        "connector": "iui_mode.hpp",
        "class_name": "IUIModeProvider",
        "types": [],
        "scope": "layers/L3_services",
        "phase": 4,
    },

    # Phase 5a: Services:: → Protocol (dispatchers)
    {
        "provider_inc": "L3_services/boundary/imotion_dispatcher.hpp",
        "provider_ns": "Services",
        "consumer_ns": "Protocol",
        "consumer_bdir": "L2_protocol/boundary",
        "connector": "imotion_dispatcher.hpp",
        "class_name": "IMotionDispatcher",
        "types": [],
        "scope": "layers/L2_protocol",
        "phase": 5,
    },
    {
        "provider_inc": "L3_services/boundary/imotor_config_dispatcher.hpp",
        "provider_ns": "Services",
        "consumer_ns": "Protocol",
        "consumer_bdir": "L2_protocol/boundary",
        "connector": "imotor_config_dispatcher.hpp",
        "class_name": "IMotorConfigDispatcher",
        "types": ["MotorConfigSnapshot"],
        "scope": "layers/L2_protocol",
        "phase": 5,
    },
    {
        "provider_inc": "L3_services/boundary/isafety_dispatcher.hpp",
        "provider_ns": "Services",
        "consumer_ns": "Protocol",
        "consumer_bdir": "L2_protocol/boundary",
        "connector": "isafety_dispatcher.hpp",
        "class_name": "ISafetyDispatcher",
        "types": ["HeartbeatStatus"],
        "scope": "layers/L2_protocol",
        "phase": 5,
    },
    {
        "provider_inc": "L3_services/boundary/iqueue_dispatcher.hpp",
        "provider_ns": "Services",
        "consumer_ns": "Protocol",
        "consumer_bdir": "L2_protocol/boundary",
        "connector": "iqueue_dispatcher.hpp",
        "class_name": "IQueueDispatcher",
        "types": [],
        "scope": "layers/L2_protocol",
        "phase": 5,
    },
    {
        "provider_inc": "L3_services/boundary/icontrol_mode_dispatcher.hpp",
        "provider_ns": "Services",
        "consumer_ns": "Protocol",
        "consumer_bdir": "L2_protocol/boundary",
        "connector": "icontrol_mode_dispatcher.hpp",
        "class_name": "IControlModeDispatcher",
        "types": [],
        "scope": "layers/L2_protocol",
        "phase": 5,
    },
    {
        "provider_inc": "L3_services/boundary/isupervisor_trim_dispatcher.hpp",
        "provider_ns": "Services",
        "consumer_ns": "Protocol",
        "consumer_bdir": "L2_protocol/boundary",
        "connector": "isupervisor_trim_dispatcher.hpp",
        "class_name": "ISupervisorTrimDispatcher",
        "types": ["SysIdSampleData", "FollowThresholds", "SupervisorTelemetry", "TrimConfig"],
        "scope": "layers/L2_protocol",
        "phase": 5,
    },
    {
        "provider_inc": "L3_services/boundary/iencoder_dispatcher.hpp",
        "provider_ns": "Services",
        "consumer_ns": "Protocol",
        "consumer_bdir": "L2_protocol/boundary",
        "connector": "iencoder_dispatcher.hpp",
        "class_name": "IEncoderDispatcher",
        "types": ["EncoderSnapshot"],
        "scope": "layers/L2_protocol",
        "phase": 5,
    },
    {
        "provider_inc": "L3_services/boundary/imotor_driver_dispatcher.hpp",
        "provider_ns": "Services",
        "consumer_ns": "Protocol",
        "consumer_bdir": "L2_protocol/boundary",
        "connector": "imotor_driver_dispatcher.hpp",
        "class_name": "IMotorDriverDispatcher",
        "types": ["FaultEnableFlags"],
        "scope": "layers/L2_protocol",
        "phase": 5,
    },
    {
        "provider_inc": "L3_services/boundary/idisplay_dispatcher.hpp",
        "provider_ns": "Services",
        "consumer_ns": "Protocol",
        "consumer_bdir": "L2_protocol/boundary",
        "connector": "idisplay_dispatcher.hpp",
        "class_name": "IDisplayDispatcher",
        "types": [],
        "scope": "layers/L2_protocol",
        "phase": 5,
    },
    {
        "provider_inc": "L3_services/boundary/iflash_image_dispatcher.hpp",
        "provider_ns": "Services",
        "consumer_ns": "Protocol",
        "consumer_bdir": "L2_protocol/boundary",
        "connector": "iflash_image_dispatcher.hpp",
        "class_name": "IFlashImageDispatcher",
        "types": ["FlashInfo"],
        "scope": "layers/L2_protocol",
        "phase": 5,
    },
    {
        "provider_inc": "L3_services/boundary/isystem_dispatcher.hpp",
        "provider_ns": "Services",
        "consumer_ns": "Protocol",
        "consumer_bdir": "L2_protocol/boundary",
        "connector": "isystem_dispatcher.hpp",
        "class_name": "ISystemDispatcher",
        "types": ["EventStats"],
        "scope": "layers/L2_protocol",
        "phase": 5,
    },
    {
        "provider_inc": "L3_services/boundary/itrace_dispatcher.hpp",
        "provider_ns": "Services",
        "consumer_ns": "Protocol",
        "consumer_bdir": "L2_protocol/boundary",
        "connector": "itrace_dispatcher.hpp",
        "class_name": "ITraceDispatcher",
        "types": ["TraceEntryData"],
        "scope": "layers/L2_protocol",
        "phase": 5,
    },
    {
        "provider_inc": "L3_services/boundary/icommand_dispatcher.hpp",
        "provider_ns": "Services",
        "consumer_ns": "Protocol",
        "consumer_bdir": "L2_protocol/boundary",
        "connector": "icommand_dispatcher.hpp",
        "class_name": "ICommandDispatcher",
        "types": [],
        "scope": "layers/L2_protocol",
        "phase": 5,
    },

    # Phase 5b: Services:: → Scheduler
    {
        "provider_inc": "L3_services/boundary/imotor_control_service.hpp",
        "provider_ns": "Services",
        "consumer_ns": "Scheduler",
        "consumer_bdir": "F_platform/tasks/boundary",
        "connector": "imotor_control_service.hpp",
        "class_name": "IMotorControlService",
        "types": [],
        "scope": "foundation/F_platform/tasks",
        "phase": 5,
    },
    {
        "provider_inc": "L3_services/boundary/imotor_event_receiver.hpp",
        "provider_ns": "Services",
        "consumer_ns": "Scheduler",
        "consumer_bdir": "F_platform/tasks/boundary",
        "connector": "imotor_event_receiver.hpp",
        "class_name": "IMotorEventReceiver",
        "types": [],
        "scope": "foundation/F_platform/tasks",
        "phase": 5,
    },
    {
        "provider_inc": "L3_services/boundary/iencoder_processing.hpp",
        "provider_ns": "Services",
        "consumer_ns": "Scheduler",
        "consumer_bdir": "F_platform/tasks/boundary",
        "connector": "iencoder_processing.hpp",
        "class_name": "IEncoderProcessing",
        "types": [],
        "scope": "foundation/F_platform/tasks",
        "phase": 5,
    },
    {
        "provider_inc": "L3_services/boundary/iencoder_status_sink.hpp",
        "provider_ns": "Services",
        "consumer_ns": "Scheduler",
        "consumer_bdir": "F_platform/tasks/boundary",
        "connector": "iencoder_status_sink.hpp",
        "class_name": "IEncoderStatusSink",
        "types": ["EncoderTelemetryData"],
        "scope": "foundation/F_platform/tasks",
        "phase": 5,
    },
    {
        "provider_inc": "L3_services/boundary/iindicator_renderer.hpp",
        "provider_ns": "Services",
        "consumer_ns": "Scheduler",
        "consumer_bdir": "F_platform/tasks/boundary",
        "connector": "iindicator_renderer.hpp",
        "class_name": "IIndicatorRenderer",
        "types": ["IndicatorParams"],
        "scope": "foundation/F_platform/tasks",
        "phase": 5,
    },
    {
        "provider_inc": "L3_services/boundary/iflash_image_access.hpp",
        "provider_ns": "Services",
        "consumer_ns": "Scheduler",
        "consumer_bdir": "F_platform/tasks/boundary",
        "connector": "iflash_image_access.hpp",
        "class_name": "IFlashImageAccess",
        "types": [],
        "scope": "foundation/F_platform/tasks",
        "phase": 5,
    },
    {
        "provider_inc": "L3_services/boundary/itrace_sink.hpp",
        "provider_ns": "Services",
        "consumer_ns": "Scheduler",
        "consumer_bdir": "F_platform/tasks/boundary",
        "connector": "itrace_sink.hpp",
        "class_name": "ITraceSink",
        "types": [],
        "scope": "foundation/F_platform/tasks",
        "phase": 5,
    },

    # Phase 6: Platform/Transport/Protocol
    {
        "provider_inc": "F_platform/boundary/iclock.hpp",
        "provider_ns": "Platform",
        "consumer_ns": "Services",
        "consumer_bdir": "L3_services/boundary",
        "connector": "iclock.hpp",
        "class_name": "IClock",
        "types": [],
        "scope": "layers/L3_services",
        "phase": 6,
    },
    {
        "provider_inc": "F_platform/boundary/ilock.hpp",
        "provider_ns": "Platform",
        "consumer_ns": "Services",
        "consumer_bdir": "L3_services/boundary",
        "connector": "ilock.hpp",
        "class_name": "ILock",
        "types": [],
        "scope": "layers/L3_services",
        "phase": 6,
    },
    {
        "provider_inc": "F_platform/boundary/iqueue.hpp",
        "provider_ns": "Platform",
        "consumer_ns": "Services",
        "consumer_bdir": "L3_services/boundary",
        "connector": "iqueue.hpp",
        "class_name": "IQueue",
        "types": [],
        "scope": "layers/L3_services",
        "phase": 6,
    },
    {
        "provider_inc": "F_platform/boundary/ischeduler.hpp",
        "provider_ns": "Platform",
        "consumer_ns": "Services",
        "consumer_bdir": "L3_services/boundary",
        "connector": "ischeduler.hpp",
        "class_name": "IScheduler",
        "types": ["TaskDescriptor", "TaskHandle"],
        "scope": "layers/L3_services",
        "phase": 6,
    },
    {
        "provider_inc": "F_platform/boundary/itask_stats.hpp",
        "provider_ns": "Platform",
        "consumer_ns": "Scheduler",
        "consumer_bdir": "F_platform/tasks/boundary",
        "connector": "itask_stats.hpp",
        "class_name": "ITaskStats",
        "types": ["TaskStat", "TaskStatsSnapshot"],
        "scope": "foundation/F_platform/tasks",
        "phase": 6,
    },
    {
        "provider_inc": "F_platform/boundary/icritical_section.hpp",
        "provider_ns": "Platform",
        "consumer_ns": "Scheduler",
        "consumer_bdir": "F_platform/tasks/boundary",
        "connector": "icritical_section.hpp",
        "class_name": "ICriticalSection",
        "types": [],
        "scope": "foundation/F_platform/tasks",
        "phase": 6,
    },
    {
        "provider_inc": "L1_transport/boundary/itransport.hpp",
        "provider_ns": "Transport",
        "consumer_ns": "Protocol",
        "consumer_bdir": "L2_protocol/boundary",
        "connector": "itransport.hpp",
        "class_name": "ITransport",
        "types": [],
        "scope": "layers/L2_protocol",
        "phase": 6,
    },
    {
        "provider_inc": "L2_protocol/boundary/icommand_processor.hpp",
        "provider_ns": "Protocol",
        "consumer_ns": "Services",
        "consumer_bdir": "L3_services/boundary",
        "connector": "icommand_processor.hpp",
        "class_name": "ICommandProcessor",
        "types": [],
        "scope": "layers/L3_services",
        "phase": 6,
    },
]


def include_to_fs(include_path: str) -> Path:
    """Convert an include path to a filesystem path."""
    for subdir in ("layers", "foundation", ""):
        p = SERVER_ROOT / subdir / include_path
        if p.is_file():
            return p
    return SERVER_ROOT / include_path


def update_connector(connector_file: str, consumer_bdir: str, dry_run: bool) -> bool:
    """Update a harness/pins/ connector to redirect to consumer port."""
    conn_path = SERVER_ROOT / "harness" / "pins" / connector_file
    if not conn_path.is_file():
        print(f"  [WARN] Connector not found: {conn_path}")
        return False

    content = conn_path.read_text(encoding='utf-8')
    new_include = f'#include "{consumer_bdir}/{connector_file}"'

    # Check if already redirected to consumer port
    if consumer_bdir in content:
        return False  # Already done

    # Replace the #include line
    new_content = re.sub(
        r'#include\s+"[^"]+/boundary/' + re.escape(connector_file) + '"',
        new_include,
        content,
    )

    if new_content == content:
        # Try a more general replacement
        new_content = re.sub(
            r'(#include\s+")[^"]+(")',
            lambda m: f'{m.group(1)}{consumer_bdir}/{connector_file}{m.group(2)}',
            content,
        )

    if new_content != content and not dry_run:
        conn_path.write_text(new_content, encoding='utf-8', newline='\n')
        return True
    return new_content != content


def find_and_replace_in_scope(scope_dir: str, old_text: str, new_text: str, dry_run: bool) -> int:
    """Find and replace text in all .hpp/.cpp files within scope directory."""
    scope_path = SERVER_ROOT / scope_dir
    if not scope_path.is_dir():
        return 0

    count = 0
    for ext in ('*.hpp', '*.cpp'):
        for fpath in scope_path.rglob(ext):
            # Skip boundary/ files (these are consumer port definitions, not consumer code)
            if '/boundary/' in str(fpath).replace('\\', '/'):
                continue
            content = fpath.read_text(encoding='utf-8', errors='replace')
            if old_text in content:
                new_content = content.replace(old_text, new_text)
                if not dry_run:
                    fpath.write_text(new_content, encoding='utf-8', newline='\n')
                count += 1

    return count


def main():
    dry_run = "--dry-run" in sys.argv
    phase_filter = None
    for i, arg in enumerate(sys.argv):
        if arg == "--phase" and i + 1 < len(sys.argv):
            phase_filter = int(sys.argv[i + 1])

    total_connectors = 0
    total_replacements = 0

    for iface in INTERFACES:
        if phase_filter and iface["phase"] != phase_filter:
            continue

        connector = iface["connector"]
        provider_ns = iface["provider_ns"]
        consumer_ns = iface["consumer_ns"]
        consumer_bdir = iface["consumer_bdir"]
        class_name = iface["class_name"]
        types = iface["types"]
        scope = iface["scope"]
        phase = iface["phase"]

        print(f"[Phase {phase}] {provider_ns}::{class_name} -> {consumer_ns}::{class_name}")

        # Step 1: Update connector
        changed = update_connector(connector, consumer_bdir, dry_run)
        if changed:
            print(f"  Connector: {connector} -> {consumer_bdir}/{connector}")
            total_connectors += 1

        # Step 2: Namespace replacements in consumer scope
        # Replace the interface class name
        all_names = [class_name] + types
        for name in all_names:
            old = f"{provider_ns}::{name}"
            new = f"{consumer_ns}::{name}"
            n = find_and_replace_in_scope(scope, old, new, dry_run)
            if n > 0:
                print(f"  Replace: {old} -> {new} ({n} files)")
                total_replacements += n

    print()
    print(f"{'DRY RUN ' if dry_run else ''}Connectors updated: {total_connectors}")
    print(f"{'DRY RUN ' if dry_run else ''}Files with replacements: {total_replacements}")


if __name__ == "__main__":
    main()
