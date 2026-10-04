#!/usr/bin/env python3
"""
batch_ports_adapters.py — Batch-generate consumer ports + adapters for Ports & Adapters.

Reads each provider interface and generates:
  1. Consumer port file (interface copied into consumer namespace)
  2. Adapter file (trivial forwarding wrapper in Harness::)
  3. Wiring map entries (printed for manual addition)

Run from project root:
    python scripts/batch_ports_adapters.py [--dry-run]
"""

import os
import re
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
SERVER_ROOT = PROJECT_ROOT / "server"

# ── Interface mapping ────────────────────────────────────────────────────────
# (provider_include_path, consumer_ns, consumer_boundary_dir,
#  adapter_name, tap_class, adapt_func_name)
#
# provider_include_path: include path relative to server/ (e.g. L5_board/boundary/igpio.hpp)
# consumer_boundary_dir: include path of consumer boundary dir (e.g. L4_drivers/boundary)

INTERFACES = [
    # ── Phase 2: Board:: → Drivers/Scheduler (4) ──────────────────────────────
    ("L5_board/boundary/igpio.hpp",              "Drivers",   "L4_drivers/boundary",          "GPIOAdapter",              "NoTap",               "adaptGPIO"),
    ("L5_board/boundary/itimer_counter.hpp",     "Scheduler", "F_platform/tasks/boundary",    "TimerCounterAdapter",      "NoTap",               "adaptTimerCounter"),
    ("L5_board/boundary/iencoder_sampler.hpp",   "Scheduler", "F_platform/tasks/boundary",    "EncoderSamplerAdapter",    "NoTap",               "adaptEncoderSampler"),
    ("L5_board/boundary/imicrosecond_clock.hpp", "Scheduler", "F_platform/tasks/boundary",    "MicrosecondClockAdapter",  "NoTap",               "adaptMicrosecondClock"),

    # ── Phase 3: Drivers:: → Services/Scheduler (4) ──────────────────────────
    ("L4_drivers/boundary/icanvas.hpp",           "Services",  "L3_services/boundary",         "CanvasAdapter",            "NoTap",               "adaptCanvas"),
    ("L4_drivers/boundary/ijoystick.hpp",         "Services",  "L3_services/boundary",         "JoystickAdapter",          "NoTap",               "adaptJoystick"),
    ("L4_drivers/boundary/iflash.hpp",            "Services",  "L3_services/boundary",         "FlashAdapter",             "NoTap",               "adaptFlash"),
    ("L4_drivers/boundary/iencoder_hardware.hpp", "Scheduler", "F_platform/tasks/boundary",    "EncoderHardwareAdapter",   "NoTap",               "adaptEncoderHardware"),

    # ── Phase 4: Scheduler:: → Services (11) ─────────────────────────────────
    ("F_platform/tasks/boundary/iencoder_filter_control.hpp", "Services", "L3_services/boundary", "EncoderFilterControlAdapter", "NoTap",          "adaptEncoderFilterControl"),
    ("F_platform/tasks/boundary/iemergency_stop.hpp",         "Services", "L3_services/boundary", "EmergencyStopAdapter",        "NoTap",          "adaptEmergencyStop"),
    ("F_platform/tasks/boundary/isafety_actions.hpp",         "Services", "L3_services/boundary", "SafetyActionsAdapter",        "TracedSafetyActions", "adaptSafetyActions"),
    ("F_platform/tasks/boundary/imotor_command_sink.hpp",     "Services", "L3_services/boundary", "MotorCommandSinkAdapter",     "TracedMotorCommandSink", "adaptMotorCommandSink"),
    ("F_platform/tasks/boundary/imotor_task_control.hpp",     "Services", "L3_services/boundary", "MotorTaskControlAdapter",     "NoTap",          "adaptMotorTaskControl"),
    ("F_platform/tasks/boundary/idebug_commands.hpp",         "Services", "L3_services/boundary", "DebugCommandsAdapter",        "NoTap",          "adaptDebugCommands"),
    ("F_platform/tasks/boundary/iremote_display.hpp",         "Services", "L3_services/boundary", "RemoteDisplayAdapter",        "NoTap",          "adaptRemoteDisplay"),
    ("F_platform/tasks/boundary/idispatch_stats.hpp",         "Services", "L3_services/boundary", "DispatchStatsAdapter",        "NoTap",          "adaptDispatchStats"),
    ("F_platform/tasks/boundary/isnapshot_writer.hpp",        "Services", "L3_services/boundary", "SnapshotWriterAdapter",       "NoTap",          "adaptSnapshotWriter"),
    ("F_platform/tasks/boundary/itelemetry.hpp",              "Services", "L3_services/boundary", "TelemetryAdapter",            "NoTap",          "adaptTelemetry"),
    ("F_platform/tasks/boundary/iui_mode.hpp",                "Services", "L3_services/boundary", "UIModeProviderAdapter",       "NoTap",          "adaptUIModeProvider"),

    # ── Phase 5a: Services:: → Protocol (13 dispatchers) ─────────────────────
    ("L3_services/boundary/imotion_dispatcher.hpp",            "Protocol", "L2_protocol/boundary", "MotionDispatcherAdapter",          "NoTap", "adaptMotionDispatcher"),
    ("L3_services/boundary/imotor_config_dispatcher.hpp",      "Protocol", "L2_protocol/boundary", "MotorConfigDispatcherAdapter",     "NoTap", "adaptMotorConfigDispatcher"),
    ("L3_services/boundary/isafety_dispatcher.hpp",            "Protocol", "L2_protocol/boundary", "SafetyDispatcherAdapter",          "NoTap", "adaptSafetyDispatcher"),
    ("L3_services/boundary/iqueue_dispatcher.hpp",             "Protocol", "L2_protocol/boundary", "QueueDispatcherAdapter",           "NoTap", "adaptQueueDispatcher"),
    ("L3_services/boundary/icontrol_mode_dispatcher.hpp",      "Protocol", "L2_protocol/boundary", "ControlModeDispatcherAdapter",     "NoTap", "adaptControlModeDispatcher"),
    ("L3_services/boundary/isupervisor_trim_dispatcher.hpp",   "Protocol", "L2_protocol/boundary", "SupervisorTrimDispatcherAdapter",  "NoTap", "adaptSupervisorTrimDispatcher"),
    ("L3_services/boundary/iencoder_dispatcher.hpp",           "Protocol", "L2_protocol/boundary", "EncoderDispatcherAdapter",         "NoTap", "adaptEncoderDispatcher"),
    ("L3_services/boundary/imotor_driver_dispatcher.hpp",      "Protocol", "L2_protocol/boundary", "MotorDriverDispatcherAdapter",     "NoTap", "adaptMotorDriverDispatcher"),
    ("L3_services/boundary/idisplay_dispatcher.hpp",           "Protocol", "L2_protocol/boundary", "DisplayDispatcherAdapter",         "NoTap", "adaptDisplayDispatcher"),
    ("L3_services/boundary/iflash_image_dispatcher.hpp",       "Protocol", "L2_protocol/boundary", "FlashImageDispatcherAdapter",      "NoTap", "adaptFlashImageDispatcher"),
    ("L3_services/boundary/isystem_dispatcher.hpp",            "Protocol", "L2_protocol/boundary", "SystemDispatcherAdapter",          "NoTap", "adaptSystemDispatcher"),
    ("L3_services/boundary/itrace_dispatcher.hpp",             "Protocol", "L2_protocol/boundary", "TraceDispatcherAdapter",           "NoTap", "adaptTraceDispatcher"),
    ("L3_services/boundary/icommand_dispatcher.hpp",           "Protocol", "L2_protocol/boundary", "CommandDispatcherAdapter",         "NoTap", "adaptCommandDispatcher"),

    # ── Phase 5b: Services:: → Scheduler (7) ─────────────────────────────────
    ("L3_services/boundary/imotor_control_service.hpp",  "Scheduler", "F_platform/tasks/boundary", "MotorControlServiceAdapter",  "NoTap", "adaptMotorControlService"),
    ("L3_services/boundary/imotor_event_receiver.hpp",   "Scheduler", "F_platform/tasks/boundary", "MotorEventReceiverAdapter",   "NoTap", "adaptMotorEventReceiver"),
    ("L3_services/boundary/iencoder_processing.hpp",     "Scheduler", "F_platform/tasks/boundary", "EncoderProcessingAdapter",    "NoTap", "adaptEncoderProcessing"),
    ("L3_services/boundary/iencoder_status_sink.hpp",    "Scheduler", "F_platform/tasks/boundary", "EncoderStatusSinkAdapter",    "NoTap", "adaptEncoderStatusSink"),
    ("L3_services/boundary/iindicator_renderer.hpp",     "Scheduler", "F_platform/tasks/boundary", "IndicatorRendererAdapter",    "NoTap", "adaptIndicatorRenderer"),
    ("L3_services/boundary/iflash_image_access.hpp",     "Scheduler", "F_platform/tasks/boundary", "FlashImageAccessAdapter",     "NoTap", "adaptFlashImageAccess"),
    ("L3_services/boundary/itrace_sink.hpp",             "Scheduler", "F_platform/tasks/boundary", "TraceSinkAdapter",            "NoTap", "adaptTraceSink"),

    # ── Phase 6: Platform/Transport/Protocol ──────────────────────────────────
    ("F_platform/boundary/iclock.hpp",            "Services",  "L3_services/boundary",         "ClockAdapter",            "NoTap",           "adaptClock"),
    ("F_platform/boundary/ilock.hpp",             "Services",  "L3_services/boundary",         "LockAdapter",             "TracedLock",      "adaptLock"),
    ("F_platform/boundary/iqueue.hpp",            "Services",  "L3_services/boundary",         "QueueAdapter",            "NoTap",           "adaptQueue"),
    ("F_platform/boundary/ischeduler.hpp",        "Services",  "L3_services/boundary",         "SchedulerAdapter",        "NoTap",           "adaptScheduler"),
    ("F_platform/boundary/itask_stats.hpp",       "Scheduler", "F_platform/tasks/boundary",    "TaskStatsAdapter",        "NoTap",           "adaptTaskStats"),
    ("F_platform/boundary/icritical_section.hpp", "Scheduler", "F_platform/tasks/boundary",    "CriticalSectionAdapter",  "NoTap",           "adaptCriticalSection"),
    ("L1_transport/boundary/itransport.hpp",      "Protocol",  "L2_protocol/boundary",         "TransportAdapter",        "TracedTransport", "adaptTransport"),
    ("L2_protocol/boundary/icommand_processor.hpp", "Services", "L3_services/boundary",        "CommandProcessorAdapter", "NoTap",           "adaptCommandProcessor"),
]


# ── Namespace → directory map ────────────────────────────────────────────────

NS_BOUNDARY_DIR = {
    "Board":     "L5_board/boundary",
    "Drivers":   "L4_drivers/boundary",
    "Services":  "L3_services/boundary",
    "Protocol":  "L2_protocol/boundary",
    "Transport": "L1_transport/boundary",
    "Scheduler": "F_platform/tasks/boundary",
    "Platform":  "F_platform/boundary",
}


# ── Filesystem helpers ───────────────────────────────────────────────────────

def include_to_fs(include_path: str) -> Path:
    """Convert an include path to a filesystem path."""
    for subdir in ("layers", "foundation", ""):
        p = SERVER_ROOT / subdir / include_path
        if p.is_file():
            return p
    return SERVER_ROOT / include_path


def consumer_fs_path(consumer_boundary_dir: str, filename: str) -> Path:
    """Get filesystem path for consumer port file."""
    if consumer_boundary_dir.startswith("L"):
        return SERVER_ROOT / "layers" / consumer_boundary_dir / filename
    elif consumer_boundary_dir.startswith("F_"):
        return SERVER_ROOT / "foundation" / consumer_boundary_dir / filename
    return SERVER_ROOT / consumer_boundary_dir / filename


def adapter_fs_path(adapter_name: str) -> Path:
    """Get filesystem path for adapter file."""
    snake = camel_to_snake(adapter_name)
    return SERVER_ROOT / "harness" / "adapters" / f"{snake}.hpp"


def camel_to_snake(name: str) -> str:
    """Convert CamelCase to snake_case (same algorithm as check_harness_ports.py)."""
    result = []
    for i, ch in enumerate(name):
        if ch.isupper() and i > 0:
            if name[i - 1].islower():
                result.append('_')
            elif name[i - 1].isupper() and i + 1 < len(name) and name[i + 1].islower() and i >= 2 and name[i - 2].isupper():
                result.append('_')
        result.append(ch.lower())
    return ''.join(result)


# ── Provider file parsing ────────────────────────────────────────────────────

def find_provider_ns(content: str) -> str:
    """Extract the first namespace from file content."""
    m = re.search(r'^\s*namespace\s+(\w+)\s*\{', content, re.MULTILINE)
    return m.group(1) if m else ""


def find_class_name(content: str) -> str:
    """Extract the main interface class/struct name (I-prefixed, non-Null)."""
    # First pass: look for I-prefixed interface (I followed by uppercase, e.g. ICanvas, ISPIBus)
    for m in re.finditer(r'^\s*(?:class|struct)\s+(\w+)\s*(?:\{|:)', content, re.MULTILINE):
        name = m.group(1)
        if len(name) >= 2 and name[0] == 'I' and name[1].isupper() and not name.startswith("Null"):
            return name
    # Fallback: first non-Null class/struct
    for m in re.finditer(r'^\s*(?:class|struct)\s+(\w+)\s*(?:\{|:)', content, re.MULTILINE):
        if not m.group(1).startswith("Null"):
            return m.group(1)
    return ""


# ── Consumer port generation ────────────────────────────────────────────────

def generate_consumer_port(
    provider_content: str,
    provider_ns: str,
    consumer_ns: str,
    provider_boundary_dir: str,
    consumer_boundary_dir: str,
    class_name: str,
) -> str:
    """Generate consumer port by namespace replacement and cleanup."""

    lines = provider_content.splitlines()
    out_lines = []
    in_null_class = False
    null_brace_depth = 0
    in_extern = False
    past_class_end = False
    class_brace_depth = 0
    in_class = False
    hit_pragma = False

    # Replace header comment
    i = 0
    # Skip existing header comment
    while i < len(lines):
        stripped = lines[i].strip()
        if stripped.startswith("/**") or stripped.startswith("*") or stripped.startswith("*/"):
            i += 1
            continue
        if stripped == "":
            i += 1
            continue
        break

    # Add new header
    out_lines.append(f"/**")
    out_lines.append(f" * @file {os.path.basename(consumer_boundary_dir)}/{class_name.lower()}.hpp")
    out_lines.append(f" * @brief Consumer port — {consumer_ns} side of the {provider_ns}::{class_name} harness connection")
    out_lines.append(f" *")
    out_lines.append(f" * Structurally identical to {provider_ns}::{class_name}.")
    out_lines.append(f" * The harness adapter bridges the provider to this consumer port.")
    out_lines.append(f" */")
    out_lines.append("")

    while i < len(lines):
        line = lines[i]
        stripped = line.strip()

        # Track Null* classes — skip them entirely
        if re.match(r'^\s*(?:class|struct)\s+Null\w+', stripped):
            in_null_class = True
            null_brace_depth = 0

        if in_null_class:
            null_brace_depth += stripped.count('{') - stripped.count('}')
            if stripped.endswith('};') or (null_brace_depth <= 0 and '}' in stripped):
                in_null_class = False
            i += 1
            continue

        # Skip extern declarations
        if stripped.startswith("extern "):
            i += 1
            continue

        # Skip free function declarations after namespace (non-class, non-struct)
        # These are accessor functions like setDispatchStatsProvider(), dispatchStats()
        if (not in_class and not stripped.startswith("namespace") and
            not stripped.startswith("//") and not stripped.startswith("/*") and
            not stripped.startswith("*") and not stripped.startswith("#") and
            not stripped.startswith("enum") and not stripped.startswith("struct") and
            not stripped.startswith("class") and not stripped.startswith("using") and
            not stripped.startswith("constexpr") and not stripped.startswith("static") and
            not stripped.startswith("}") and not stripped.startswith("{") and
            not stripped == "" and
            re.match(r'^\s*\w+.*\(.*\)', stripped) and
            '= 0' not in stripped and
            hit_pragma):
            # Check if it looks like a free function (not a method)
            if not any(kw in stripped for kw in ['virtual', 'override']):
                i += 1
                continue

        # Replace namespace
        if re.match(rf'^\s*namespace\s+{re.escape(provider_ns)}\s*\{{', stripped):
            out_lines.append(line.replace(f"namespace {provider_ns}", f"namespace {consumer_ns}"))
            i += 1
            continue

        # Replace closing namespace comment
        if re.match(rf'^\s*\}}\s*//\s*namespace\s+{re.escape(provider_ns)}', stripped):
            out_lines.append(line.replace(f"namespace {provider_ns}", f"namespace {consumer_ns}"))
            i += 1
            continue

        # Replace include paths from provider boundary to consumer boundary
        inc_match = re.match(r'^(\s*#include\s+")(.+?)(".*)', line)
        if inc_match:
            prefix, inc_path, suffix = inc_match.groups()
            # Replace same-namespace boundary includes
            if inc_path.startswith(provider_boundary_dir + "/"):
                inc_path = inc_path.replace(provider_boundary_dir + "/",
                                           consumer_boundary_dir + "/", 1)
            out_lines.append(f"{prefix}{inc_path}{suffix}")
            i += 1
            continue

        # Track class body for free-function detection
        if re.match(r'^\s*class\s+\w+', stripped) and not stripped.startswith("class Null"):
            in_class = True
            class_brace_depth = 0

        if in_class:
            class_brace_depth += stripped.count('{') - stripped.count('}')
            if class_brace_depth <= 0 and '}' in stripped:
                in_class = False

        if stripped == "#pragma once":
            hit_pragma = True

        out_lines.append(line)
        i += 1

    return "\n".join(out_lines) + "\n"


# ── Adapter generation ──────────────────────────────────────────────────────

def parse_virtual_methods(content: str, class_name: str):
    """Parse pure virtual methods from the class (handles multi-line declarations)."""
    methods = []
    lines = content.splitlines()
    in_class = False
    brace_depth = 0

    # First pass: find the class body lines
    class_lines = []
    for line in lines:
        stripped = line.strip()

        if not in_class:
            if re.match(rf'^\s*(?:class|struct)\s+{re.escape(class_name)}\s*(?:\{{|:)', stripped):
                in_class = True
                if '{' in stripped:
                    brace_depth = 1
                continue
            continue

        brace_depth += stripped.count('{') - stripped.count('}')
        if brace_depth <= 0:
            break
        class_lines.append(stripped)

    # Join lines to handle multi-line declarations, then split on ';'
    joined = ' '.join(class_lines)
    # Split on semicolons to get individual statements
    statements = joined.split(';')

    for stmt in statements:
        stmt = stmt.strip()
        # Match pure virtual methods (= 0 at end), allowing public:/private: prefix
        m = re.search(
            r'virtual\s+(.+?)\s+(\w+)\s*\(([^)]*)\)\s*(const\s*)?=\s*0\s*$',
            stmt
        )
        if m:
            methods.append({
                'return_type': m.group(1).strip(),
                'name': m.group(2).strip(),
                'params': m.group(3).strip(),
                'const': m.group(4) is not None,
            })

    return methods


def extract_param_names(params: str) -> str:
    """Extract parameter names from a parameter list for forwarding."""
    if not params.strip():
        return ""
    names = []
    # Split on commas (respecting angle brackets)
    depth = 0
    current = []
    for ch in params:
        if ch in '<(':
            depth += 1
        elif ch in '>)':
            depth -= 1
        elif ch == ',' and depth == 0:
            names.append(_extract_name(''.join(current)))
            current = []
            continue
        current.append(ch)
    if current:
        names.append(_extract_name(''.join(current)))
    return ', '.join(names)


def _extract_name(param: str) -> str:
    """Extract the parameter name from a single parameter."""
    param = param.split('=')[0].strip()  # Remove defaults
    tokens = param.split()
    name = tokens[-1].lstrip('*&') if tokens else param
    return name


def generate_adapter(
    provider_ns: str,
    consumer_ns: str,
    class_name: str,
    adapter_name: str,
    methods: list,
    provider_include: str,
    consumer_include: str,
) -> str:
    """Generate the adapter .hpp file."""
    snake = camel_to_snake(adapter_name)
    lines = []
    lines.append(f"/**")
    lines.append(f" * @file {snake}.hpp")
    lines.append(f" * @brief Harness adapter: {provider_ns}::{class_name} --> {consumer_ns}::{class_name}")
    lines.append(f" *")
    lines.append(f" * Auto-generated by batch_ports_adapters.py.")
    lines.append(f" * Trivial forwarder — both interfaces are structurally identical.")
    lines.append(f" */")
    lines.append(f"")
    lines.append(f"#pragma once")
    lines.append(f"")
    lines.append(f'#include "{provider_include}"')
    lines.append(f'#include "{consumer_include}"')
    lines.append(f"")
    lines.append(f"namespace Harness {{")
    lines.append(f"")
    lines.append(f"class {adapter_name} : public {consumer_ns}::{class_name} {{")
    lines.append(f"public:")
    lines.append(f"    explicit {adapter_name}({provider_ns}::{class_name}& provider)"
                 f" : m_provider(provider) {{}}")

    for method in methods:
        const = " const" if method['const'] else ""
        pnames = extract_param_names(method['params'])
        rt = method['return_type']
        lines.append(f"")
        if rt == "void":
            lines.append(f"    void {method['name']}({method['params']}){const} override {{")
            lines.append(f"        m_provider.{method['name']}({pnames});")
            lines.append(f"    }}")
        else:
            lines.append(f"    {rt} {method['name']}({method['params']}){const} override {{")
            lines.append(f"        return m_provider.{method['name']}({pnames});")
            lines.append(f"    }}")

    lines.append(f"")
    lines.append(f"private:")
    lines.append(f"    {provider_ns}::{class_name}& m_provider;")
    lines.append(f"}};")
    lines.append(f"")
    lines.append(f"}} // namespace Harness")
    lines.append(f"")
    return "\n".join(lines)


# ── Wiring map entry generation ─────────────────────────────────────────────

def generate_wiring_entries(entries):
    """Generate HARNESS and ADAPTER tag lines + adapt function stubs."""
    harness_lines = []
    adapter_lines = []
    adapt_decls = []
    adapt_impls = []

    for e in entries:
        provider_ns = e['provider_ns']
        class_name = e['class_name']
        consumer_ns = e['consumer_ns']
        adapter_name = e['adapter_name']
        tap_class = e['tap_class']
        adapt_func = e['adapt_func']
        consumer_include = e['consumer_include']
        adapter_snake = camel_to_snake(adapter_name)

        harness_lines.append(
            f"// HARNESS: {adapt_func}  {provider_ns}::{class_name}  "
            f"->  {consumer_ns}::{class_name}  [{adapter_name}, {tap_class}]"
        )
        adapter_lines.append(
            f"// ADAPTER: {adapter_snake}.hpp  {adapter_name}  "
            f"[{provider_ns}::{class_name} -> {consumer_ns}::{class_name}]"
        )

        # Declaration
        adapt_decls.append(
            f"{consumer_ns}::{class_name}& {adapt_func}({provider_ns}::{class_name}& raw);"
        )

        # Implementation
        if tap_class == "NoTap":
            adapt_impls.append(
                f"{consumer_ns}::{class_name}& Harness::{adapt_func}({provider_ns}::{class_name}& raw) {{\n"
                f"    return harness<{provider_ns}::{class_name}, {consumer_ns}::{class_name}, "
                f"{adapter_name}, NoTap>(raw);\n}}"
            )
        else:
            adapt_impls.append(
                f"{consumer_ns}::{class_name}& Harness::{adapt_func}({provider_ns}::{class_name}& raw) {{\n"
                f"    return harness<{provider_ns}::{class_name}, {consumer_ns}::{class_name}, "
                f"{adapter_name}, {tap_class}>(raw);\n}}"
            )

    return harness_lines, adapter_lines, adapt_decls, adapt_impls


# ── Special case: IQueue template ────────────────────────────────────────────

SKIP_ADAPTER_GENERATION = {
    "iqueue.hpp",           # Template class — adapter needs manual handling
    "icommand_dispatcher.hpp",  # Composite — inherits 12 sub-dispatchers
}


# ── Main ─────────────────────────────────────────────────────────────────────

def main():
    dry_run = "--dry-run" in sys.argv

    created_ports = 0
    created_adapters = 0
    skipped = 0
    errors = []
    wiring_entries = []

    print(f"{'DRY RUN — ' if dry_run else ''}Generating Ports & Adapters files...")
    print(f"Server root: {SERVER_ROOT}")
    print()

    for (provider_inc, consumer_ns, consumer_bdir, adapter_name, tap_class, adapt_func) in INTERFACES:
        filename = os.path.basename(provider_inc)
        provider_fs = include_to_fs(provider_inc)

        if not provider_fs.is_file():
            errors.append(f"Provider not found: {provider_inc}")
            continue

        provider_content = provider_fs.read_text(encoding='utf-8', errors='replace')
        provider_ns = find_provider_ns(provider_content)
        class_name = find_class_name(provider_content)

        if not provider_ns or not class_name:
            errors.append(f"Cannot parse {provider_inc}: ns={provider_ns}, class={class_name}")
            continue

        provider_bdir = os.path.dirname(provider_inc)
        consumer_include = f"{consumer_bdir}/{filename}"

        # ── Generate consumer port ────────────────────────────────────────
        port_path = consumer_fs_path(consumer_bdir, filename)

        if port_path.is_file():
            print(f"  [SKIP] Consumer port exists: {port_path.relative_to(SERVER_ROOT)}")
            skipped += 1
        else:
            port_content = generate_consumer_port(
                provider_content, provider_ns, consumer_ns,
                provider_bdir, consumer_bdir, class_name,
            )
            if not dry_run:
                port_path.parent.mkdir(parents=True, exist_ok=True)
                port_path.write_text(port_content, encoding='utf-8', newline='\n')
            print(f"  [PORT] {port_path.relative_to(SERVER_ROOT)}")
            created_ports += 1

        # ── Generate adapter ──────────────────────────────────────────────
        adp_path = adapter_fs_path(adapter_name)

        if adp_path.is_file():
            print(f"  [SKIP] Adapter exists: {adp_path.relative_to(SERVER_ROOT)}")
        elif filename in SKIP_ADAPTER_GENERATION:
            print(f"  [MANUAL] Adapter needs manual handling: {adapter_name} ({filename})")
        else:
            methods = parse_virtual_methods(provider_content, class_name)
            if not methods:
                print(f"  [WARN] No virtual methods found in {class_name}")

            adp_content = generate_adapter(
                provider_ns, consumer_ns, class_name, adapter_name,
                methods, provider_inc, consumer_include,
            )
            if not dry_run:
                adp_path.parent.mkdir(parents=True, exist_ok=True)
                adp_path.write_text(adp_content, encoding='utf-8', newline='\n')
            print(f"  [ADPT] {adp_path.relative_to(SERVER_ROOT)}")
            created_adapters += 1

        wiring_entries.append({
            'provider_ns': provider_ns,
            'class_name': class_name,
            'consumer_ns': consumer_ns,
            'adapter_name': adapter_name,
            'tap_class': tap_class,
            'adapt_func': adapt_func,
            'consumer_include': consumer_include,
        })

    # ── Print wiring map entries ──────────────────────────────────────────
    harness_tags, adapter_tags, decls, impls = generate_wiring_entries(wiring_entries)

    print()
    print("=" * 70)
    print("WIRING MAP ADDITIONS")
    print("=" * 70)

    print("\n// -- HARNESS tags (add to harness_wiring_map.hpp) --")
    for line in harness_tags:
        print(line)

    print("\n// -- ADAPTER tags (add to harness_wiring_map.hpp) --")
    for line in adapter_tags:
        print(line)

    print("\n// -- Declarations (add to harness_wiring_map.hpp namespace Harness) --")
    for line in decls:
        print(f"    {line}")

    print("\n// -- Implementations (add to harness_wiring_map.cpp) --")
    for impl in impls:
        print(impl)
        print()

    # -- Summary --
    print("=" * 70)
    print(f"Consumer ports created:  {created_ports}")
    print(f"Adapters created:       {created_adapters}")
    print(f"Skipped (existing):     {skipped}")
    if errors:
        print(f"Errors: {len(errors)}")
        for e in errors:
            print(f"  - {e}")


if __name__ == "__main__":
    main()
