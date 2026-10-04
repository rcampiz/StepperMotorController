#!/usr/bin/env python3
"""
Regenerate provider boundary files from the original (git-committed) connector content.

Takes the committed harness/pins/*.hpp connector content and produces a correct
provider boundary file by changing only the namespace declaration.
Cross-namespace references (like Drivers::IMotorDriver) are preserved as-is.
"""

import os
import re
import subprocess
import sys

SERVER_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "server")

# Mapping: connector filename -> (provider boundary path relative to SERVER_DIR, provider namespace)
INTERFACES = {
    # Scheduler:: (12) -> F_platform/tasks/boundary/
    "iencoder.hpp": ("foundation/F_platform/tasks/boundary/iencoder.hpp", "Scheduler"),
    "iencoder_filter_control.hpp": ("foundation/F_platform/tasks/boundary/iencoder_filter_control.hpp", "Scheduler"),
    "iemergency_stop.hpp": ("foundation/F_platform/tasks/boundary/iemergency_stop.hpp", "Scheduler"),
    "isafety_actions.hpp": ("foundation/F_platform/tasks/boundary/isafety_actions.hpp", "Scheduler"),
    "imotor_command_sink.hpp": ("foundation/F_platform/tasks/boundary/imotor_command_sink.hpp", "Scheduler"),
    "imotor_task_control.hpp": ("foundation/F_platform/tasks/boundary/imotor_task_control.hpp", "Scheduler"),
    "idebug_commands.hpp": ("foundation/F_platform/tasks/boundary/idebug_commands.hpp", "Scheduler"),
    "iremote_display.hpp": ("foundation/F_platform/tasks/boundary/iremote_display.hpp", "Scheduler"),
    "idispatch_stats.hpp": ("foundation/F_platform/tasks/boundary/idispatch_stats.hpp", "Scheduler"),
    "isnapshot_writer.hpp": ("foundation/F_platform/tasks/boundary/isnapshot_writer.hpp", "Scheduler"),
    "itelemetry.hpp": ("foundation/F_platform/tasks/boundary/itelemetry.hpp", "Scheduler"),
    "iui_mode.hpp": ("foundation/F_platform/tasks/boundary/iui_mode.hpp", "Scheduler"),

    # Services:: (20) -> L3_services/boundary/
    "imotion_dispatcher.hpp": ("layers/L3_services/boundary/imotion_dispatcher.hpp", "Services"),
    "isafety_dispatcher.hpp": ("layers/L3_services/boundary/isafety_dispatcher.hpp", "Services"),
    "iqueue_dispatcher.hpp": ("layers/L3_services/boundary/iqueue_dispatcher.hpp", "Services"),
    "icontrol_mode_dispatcher.hpp": ("layers/L3_services/boundary/icontrol_mode_dispatcher.hpp", "Services"),
    "isupervisor_trim_dispatcher.hpp": ("layers/L3_services/boundary/isupervisor_trim_dispatcher.hpp", "Services"),
    "iencoder_dispatcher.hpp": ("layers/L3_services/boundary/iencoder_dispatcher.hpp", "Services"),
    "imotor_driver_dispatcher.hpp": ("layers/L3_services/boundary/imotor_driver_dispatcher.hpp", "Services"),
    "imotor_config_dispatcher.hpp": ("layers/L3_services/boundary/imotor_config_dispatcher.hpp", "Services"),
    "idisplay_dispatcher.hpp": ("layers/L3_services/boundary/idisplay_dispatcher.hpp", "Services"),
    "iflash_image_dispatcher.hpp": ("layers/L3_services/boundary/iflash_image_dispatcher.hpp", "Services"),
    "isystem_dispatcher.hpp": ("layers/L3_services/boundary/isystem_dispatcher.hpp", "Services"),
    "itrace_dispatcher.hpp": ("layers/L3_services/boundary/itrace_dispatcher.hpp", "Services"),
    "icommand_dispatcher.hpp": ("layers/L3_services/boundary/icommand_dispatcher.hpp", "Services"),
    "iencoder_processing.hpp": ("layers/L3_services/boundary/iencoder_processing.hpp", "Services"),
    "iencoder_status_sink.hpp": ("layers/L3_services/boundary/iencoder_status_sink.hpp", "Services"),
    "iflash_image_access.hpp": ("layers/L3_services/boundary/iflash_image_access.hpp", "Services"),
    "iindicator_renderer.hpp": ("layers/L3_services/boundary/iindicator_renderer.hpp", "Services"),
    "imotor_control_service.hpp": ("layers/L3_services/boundary/imotor_control_service.hpp", "Services"),
    "imotor_event_receiver.hpp": ("layers/L3_services/boundary/imotor_event_receiver.hpp", "Services"),
    "itrace_sink.hpp": ("layers/L3_services/boundary/itrace_sink.hpp", "Services"),

    # Transport:: (2) -> L1_transport/boundary/
    "itransport.hpp": ("layers/L1_transport/boundary/itransport.hpp", "Transport"),
    "ibyte_channel.hpp": ("layers/L1_transport/boundary/ibyte_channel.hpp", "Transport"),

    # Protocol:: (1) -> L2_protocol/boundary/
    "icommand_processor.hpp": ("layers/L2_protocol/boundary/icommand_processor.hpp", "Protocol"),
}


def get_committed_content(connector_filename):
    """Get the original connector content from git HEAD."""
    git_path = f"server/harness/pins/{connector_filename}"
    try:
        result = subprocess.run(
            ["git", "show", f"HEAD:{git_path}"],
            capture_output=True, text=True, check=True
        )
        return result.stdout
    except subprocess.CalledProcessError:
        print(f"  WARNING: Could not read from git: {git_path}")
        return None


def transform_namespace(content, target_ns):
    """Change namespace Harness to target namespace, preserving everything else.
    Does NOT touch standalone forward declarations (handled by qualify step)."""

    # First, handle standalone forward declarations separately.
    # These need to use the CORRECT namespace for each type, not target_ns.
    # Remove them for now; they'll be re-added by qualify_forward_decls.
    fwd_decl_pattern = r'namespace\s+Harness\s*\{\s*class\s+\w+\s*;\s*\}\s*\n?'
    fwd_decls = re.findall(fwd_decl_pattern, content)
    content = re.sub(fwd_decl_pattern, '', content)

    # Replace main namespace declaration blocks
    content = re.sub(
        r'namespace\s+Harness\s*\{',
        f'namespace {target_ns} {{',
        content
    )
    # Replace closing comment
    content = content.replace('} // namespace Harness', f'}} // namespace {target_ns}')

    return content, fwd_decls


# Cross-namespace types that were previously unqualified inside namespace Harness {}.
# After changing to a different namespace, these need explicit qualification.
# Only apply within provider boundary files (not connector files).
CROSS_NS_QUALIFICATIONS = {
    # Types that stay in Harness:: (cross-layer types, not migrated)
    "ServiceStatus": "Harness::ServiceStatus",
    "MotorDebugParams": "Harness::MotorDebugParams",
    "EncoderFilterConfig": "Harness::EncoderFilterConfig",
    "EncoderStatus": "Harness::EncoderStatus",

    # Types that migrated to Scheduler:: (from other connectors)
    "MotorCommand": "Scheduler::MotorCommand",
    "MotorCmdType": "Scheduler::MotorCmdType",
    "DispatchStats": "Scheduler::DispatchStats",
    "EncoderSnapshot": "Scheduler::EncoderSnapshot",
    "IEncoder": "Scheduler::IEncoder",
    "IEncoderFilterControl": "Scheduler::IEncoderFilterControl",
    "IEmergencyStop": "Scheduler::IEmergencyStop",
    "ISafetyActions": "Scheduler::ISafetyActions",
    "IMotorCommandSink": "Scheduler::IMotorCommandSink",
    "IMotorTaskControl": "Scheduler::IMotorTaskControl",
    "IDebugCommands": "Scheduler::IDebugCommands",
    "IRemoteDisplay": "Scheduler::IRemoteDisplay",
    "IDispatchStats": "Scheduler::IDispatchStats",
    "ISnapshotWriter": "Scheduler::ISnapshotWriter",
    "ITelemetry": "Scheduler::ITelemetry",
    "IUIModeProvider": "Scheduler::IUIModeProvider",
    "EncoderTelemetryData": "Services::EncoderTelemetryData",
    "IndicatorParams": "Services::IndicatorParams",

    # Types that migrated to Transport::
    "ITransport": "Transport::ITransport",
    "IByteChannel": "Transport::IByteChannel",

    # Types that migrated to Protocol::
    "ICommandProcessor": "Protocol::ICommandProcessor",

    # Types that migrated to Services::
    "IMotionDispatcher": "Services::IMotionDispatcher",
    "ISafetyDispatcher": "Services::ISafetyDispatcher",
    "IQueueDispatcher": "Services::IQueueDispatcher",
    "IControlModeDispatcher": "Services::IControlModeDispatcher",
    "ISupervisorTrimDispatcher": "Services::ISupervisorTrimDispatcher",
    "IEncoderDispatcher": "Services::IEncoderDispatcher",
    "IMotorDriverDispatcher": "Services::IMotorDriverDispatcher",
    "IMotorConfigDispatcher": "Services::IMotorConfigDispatcher",
    "IDisplayDispatcher": "Services::IDisplayDispatcher",
    "IFlashImageDispatcher": "Services::IFlashImageDispatcher",
    "ISystemDispatcher": "Services::ISystemDispatcher",
    "ITraceDispatcher": "Services::ITraceDispatcher",
    "ICommandDispatcher": "Services::ICommandDispatcher",
    "IEncoderProcessing": "Services::IEncoderProcessing",
    "IEncoderStatusSink": "Services::IEncoderStatusSink",
    "IFlashImageAccess": "Services::IFlashImageAccess",
    "IIndicatorRenderer": "Services::IIndicatorRenderer",
    "IMotorControlService": "Services::IMotorControlService",
    "IMotorEventReceiver": "Services::IMotorEventReceiver",
    "ITraceSink": "Services::ITraceSink",
    "SysIdSampleData": "Services::SysIdSampleData",
}


def rebuild_forward_decls(fwd_decls):
    """Convert extracted Harness:: forward declarations to use the correct namespace.
    E.g. 'namespace Harness { class ITransport; }' -> 'namespace Transport { class ITransport; }'
    """
    result = []
    class_pattern = re.compile(r'class\s+(\w+)\s*;')
    for decl in fwd_decls:
        m = class_pattern.search(decl)
        if not m:
            continue
        class_name = m.group(1)
        # Look up the correct namespace for this type
        if class_name in CROSS_NS_QUALIFICATIONS:
            correct_ns = CROSS_NS_QUALIFICATIONS[class_name].split("::")[0]
        else:
            correct_ns = "Harness"  # fallback: keep in Harness
        result.append(f"namespace {correct_ns} {{ class {class_name}; }}\n")
    return result


def qualify_cross_namespace_refs(content, target_ns):
    """Add namespace qualifiers to unqualified cross-namespace type references.
    Only qualifies types that are NOT in the target namespace."""

    # Detect nested struct/class definitions in this file — these should NOT be qualified
    # because they're local to the class, not cross-namespace references
    nested_defs = set()
    for m in re.finditer(r'(?:struct|class)\s+(\w+)\s*\{', content):
        nested_defs.add(m.group(1))

    for unqualified, qualified in CROSS_NS_QUALIFICATIONS.items():
        # Skip if the type is in the same namespace as the target
        qual_ns = qualified.split("::")[0]
        if qual_ns == target_ns:
            continue  # Same namespace — unqualified access works

        # 1. Replace Harness::TypeName -> CorrectNS::TypeName
        old_harness_ref = f'Harness::{unqualified}'
        content = content.replace(old_harness_ref, qualified)

        # 2. Replace unqualified TypeName -> CorrectNS::TypeName
        # Skip if this type is defined as a nested struct/class in this file
        if unqualified in nested_defs:
            continue

        # Skip struct/class definitions (preceded by 'struct ' or 'class ')
        # Skip if already namespace-qualified (preceded by ::)
        pattern = rf'(?<!::)(?<!\w)(?<!struct )(?<!class ){re.escape(unqualified)}(?=\W)'
        content = re.sub(pattern, qualified, content)

    return content


def main():
    dry_run = "--dry-run" in sys.argv
    print(f"Regenerating {len(INTERFACES)} provider boundary files from git")
    if dry_run:
        print("(DRY RUN)")
    print()

    generated = 0
    for connector_name, (boundary_rel_path, target_ns) in INTERFACES.items():
        content = get_committed_content(connector_name)
        if content is None:
            continue

        # Transform namespace (returns content + extracted forward declarations)
        new_content, fwd_decls = transform_namespace(content, target_ns)

        # Qualify cross-namespace type references
        new_content = qualify_cross_namespace_refs(new_content, target_ns)

        # Re-insert forward declarations with correct namespaces
        if fwd_decls:
            rebuilt = rebuild_forward_decls(fwd_decls)
            # Insert before the main namespace block
            ns_match = re.search(rf'namespace\s+{re.escape(target_ns)}\s*\{{', new_content)
            if ns_match:
                insert_pos = ns_match.start()
                new_content = new_content[:insert_pos] + ''.join(rebuilt) + '\n' + new_content[insert_pos:]
            else:
                # Fallback: prepend after #pragma once / #includes
                new_content = ''.join(rebuilt) + '\n' + new_content

        boundary_path = os.path.join(SERVER_DIR, boundary_rel_path)

        if dry_run:
            print(f"  [DRY] {connector_name} -> {boundary_rel_path} ({target_ns}::)")
        else:
            os.makedirs(os.path.dirname(boundary_path), exist_ok=True)
            with open(boundary_path, 'w', newline='\n') as f:
                f.write(new_content)
            print(f"  Generated: {boundary_rel_path} ({target_ns}::)")

        generated += 1

    print(f"\nRegenerated: {generated} files")


if __name__ == "__main__":
    main()
