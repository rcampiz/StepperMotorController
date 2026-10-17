#!/usr/bin/env python3
"""
gen_architecture.py -- generate docs/ARCHITECTURE.md and docs/architecture_manifest.json
from the harness definition tables (server/harness/defs/harness_*.hpp) and the source tree.

The *_SIGNALS / *_DESC constexpr tables are the single source of truth for every boundary;
this script turns them into the human-readable document, the manifest JSON (same schema
as the Phase 1 discovery report) and a Mermaid diagram.

Usage: python scripts/arch/gen_architecture.py [--check]   (--check: exit 1 if outputs are stale)
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SERVER = os.path.join(ROOT, "server")
DEFS = os.path.join(SERVER, "harness", "defs")
DOCS = os.path.join(ROOT, "docs")

SIGNALS_RE = re.compile(r'constexpr\s+SignalDesc\s+(\w+)\[\]\s*=\s*\{(.*?)\};', re.S)
ROW_RE = re.compile(r'\{\s*"([^"]*)"\s*,\s*Provider::(\w+)\s*,\s*Flow::(\w+)\s*,\s*Timing::(\w+)\s*,\s*ctx\(([^)]*)\)\s*,\s*"([^"]*)"\s*\}')
DESC_RE = re.compile(r'constexpr\s+HarnessDesc\s+(\w+)\s*=\s*\{\s*"([^"]+)"\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*(\w+)\s*,\s*(\d+)\s*\}')
BRIEF_RE = re.compile(r'@brief\s+(.*)')

LAYERS = [
    ("L1", "Transport", "server/layers/L1_transport", "Byte-stream ingress/egress to the host (VCP UART / RTT); owns ITransport and the IByteChannel contract that L5 implements."),
    ("L2", "Protocol", "server/layers/L2_protocol", "SCPI parsing, validation, aliases, all host-facing formatting (responses, events, telemetry, joystick, banner); routes to the 13 L3 dispatchers."),
    ("L3", "Services", "server/layers/L3_services", "All business logic and safety, host-testable, no RTOS loops: Motor/Comms/Encoder/Display/Debug services, controllers, dispatchers, telemetry store, UI, trace."),
    ("L4", "Drivers", "server/layers/L4_drivers", "Device drivers over Board pin bundles: powerSTEP01, ST7789, NOR flash, joystick, quadrature encoder (+ index ISR), config store, debug access."),
    ("L5", "Board", "server/layers/L5_board", "Register/peripheral access: clocks, GPIO, SPI1/SPI2, USART2 + RTT byte channels, TIM4, TIM3+DMA sampler, EXTI, TE sync, internal flash, bring-up commands."),
    ("TASKS", "Task sheet (Scheduler::)", "server/foundation/F_platform/tasks", "Four RTOS task loops that only wait, then call one L3 entry point; owns priorities, periods, stack sizes, task handles and the task-init contracts the composition root calls."),
    ("PLATFORM", "Platform (power net)", "server/foundation/F_platform", "RTOS-neutral primitives any sheet may use: clock, microsecond clock, locks, queues, scheduler, task stats, critical section, task notifier; FreeRTOS implementations, startup."),
    ("HARNESS", "Harness", "server/harness", "Harness definitions (defs/), connect() functions (the only place taps are applied), shared POD pins, traced taps, interface trace."),
    ("SYSTEM", "Composition root", "server", "main.cpp + system_init.cpp: phase sequencing, Platform resources, RTOS task creation over the L3 services."),
]

TASKS = [
    {"name": "Motor",   "priority": 4, "stack": 1024, "trigger": "event",    "period_ms": 50, "layer_affinity": ["TASKS", "L3"], "body": "queue receive (50 ms timeout) -> IMotorService::onCommand; every 50 ms -> IMotorService::periodicUpdate"},
    {"name": "Comms",   "priority": 3, "stack": 2048, "trigger": "event",    "period_ms": 10, "layer_affinity": ["TASKS", "L3"], "body": "ITaskNotifier wait (USART2 RX wake) with 10 ms fallback -> ICommsService::tick"},
    {"name": "Encoder", "priority": 2, "stack": 256,  "trigger": "periodic", "period_ms": 10, "layer_affinity": ["TASKS", "L3"], "body": "sleepUntil 10 ms -> IEncoderService::sample (task not created without encoder hardware)"},
    {"name": "Display", "priority": 1, "stack": 640,  "trigger": "periodic", "period_ms": 50, "layer_affinity": ["TASKS", "L3"], "body": "sleepUntil 50 ms -> IDisplayService::tick"},
]

HARNESS_FILES = {
    "L1_L2": "harness_l1_l2.hpp", "L2_L3": "harness_l2_l3.hpp", "L3_L4": "harness_l3_l4.hpp",
    "L4_L5": "harness_l4_l5.hpp", "TASKS_L3": "harness_tasks_l3.hpp", "L5_L1": "harness_l5_l1_bytes.hpp",
}


def read(path):
    with open(path, encoding="utf-8", errors="ignore") as fh:
        return fh.read()


def list_files(rel_dir):
    out = []
    base = os.path.join(ROOT, rel_dir)
    for d, _, files in os.walk(base):
        if "external" in d.replace("\\", "/"):
            continue
        for f in sorted(files):
            if f.endswith((".cpp", ".hpp", ".h", ".c", ".s")):
                rel = os.path.relpath(os.path.join(d, f), ROOT).replace("\\", "/")
                if rel_dir == "server" and "/" in rel[len("server/"):]:
                    continue   # root sheet: only server/*.cpp
                out.append(rel)
    return out


SHEET_PREFIX = [
    ("server/layers/L1_transport/", "L1"), ("server/layers/L2_protocol/", "L2"),
    ("server/layers/L3_services/", "L3"), ("server/layers/L4_drivers/", "L4"),
    ("server/layers/L5_board/", "L5"), ("server/foundation/F_platform/tasks/", "TASKS"),
    ("server/foundation/F_platform/", "PLATFORM"), ("server/harness/", "HARNESS"),
]


def sheet_of(rel):
    for prefix, sheet in SHEET_PREFIX:
        if rel.startswith(prefix):
            return sheet
    return "SYSTEM"


def includers_of(prefix):
    """Count, per sheet, the source files that include a header starting with `prefix`."""
    counts = {}
    for d, _, files in os.walk(SERVER):
        if "external" in d.replace("\\", "/"):
            continue
        for f in files:
            if not f.endswith((".cpp", ".hpp", ".h")):
                continue
            path = os.path.join(d, f)
            rel = os.path.relpath(path, ROOT).replace("\\", "/")
            if f'#include "{prefix}' in read(path):
                sheet = sheet_of(rel)
                counts[sheet] = counts.get(sheet, 0) + 1
    return dict(sorted(counts.items()))


def global_nets():
    return [
        {"id": "PLATFORM", "name": "Platform power net", "path": "server/foundation/F_platform/boundary",
         "rule": "rule 4: RTOS-neutral primitives (clock, us clock, locks, queues, scheduler, task stats, critical section, notifier) usable by any sheet without a harness",
         "direct_includers": includers_of("F_platform/boundary/")},
        {"id": "TRACE", "name": "Interface trace net", "path": "server/harness/trace",
         "rule": "second global net: ITRACE()/ITrace::log and the trace sink may be included by any sheet; compiled to no-ops unless ENABLE_INTERFACE_TRACE (firmware-debug); sink = L3 trace ring via Trace::initTraceSink",
         "direct_includers": includers_of("harness/trace/")},
    ]


def parse_harnesses():
    result = []
    for path in sorted(os.listdir(DEFS)):
        if not (path.startswith("harness_") and path.endswith(".hpp")) or path == "harness_signal.hpp":
            continue
        text = read(os.path.join(DEFS, path))
        tables = {m.group(1): m.group(2) for m in SIGNALS_RE.finditer(text)}
        for m in DESC_RE.finditer(text):
            rows = [
                {"name": r.group(1), "provider": r.group(2), "flow": r.group(3), "timing": r.group(4),
                 "ctx": [c.strip().replace("Ctx::", "") for c in r.group(5).split("|")],
                 "ownership": r.group(6)}
                for r in ROW_RE.finditer(tables.get(m.group(5), ""))
            ]
            brief = BRIEF_RE.search(text)
            result.append({
                "id": m.group(2), "upper": m.group(3), "lower": m.group(4),
                "file": "server/harness/defs/" + path, "brief": brief.group(1).strip() if brief else "",
                "signals": rows,
            })
    return result


def build_manifest(harnesses):
    layers = []
    for lid, name, rel, resp in LAYERS:
        layers.append({"id": lid, "name": name, "responsibility": resp, "files": list_files(rel)})
    hs = []
    edges = []
    for h in harnesses:
        exc = h["id"] == "L5_L1"
        hs.append({
            "id": "H_" + h["id"], "between": [h["upper"], h["lower"]],
            "bundles_contracts": [s["name"] for s in h["signals"]],
            "status": "implemented", "defined_in": h["file"], "notes": h["brief"],
            **({"exception": True} if exc else {}),
        })
        for s in h["signals"]:
            frm, to = (h["upper"], h["lower"]) if s["flow"] == "Down" else (h["lower"], h["upper"])
            if s["flow"] == "Bidir":
                frm, to = h["upper"], h["lower"]
            edges.append({
                "from": frm, "to": to, "via": "harness", "name": s["name"], "data": s["ownership"],
                "direction": s["flow"].lower(),
                "sync": "sync" if s["timing"] == "Sync" else "async",
                "is_violation": False, **({"exception": True} if exc else {}),
            })
    return {
        "project": {
            "name": "StepperMotorController (STM32F401RE stepper motor controller)",
            "mcu": "STM32F401RE, Cortex-M4F @ 84 MHz, NUCLEO-F401RE + IHM03A1 (powerSTEP01) + GFX01M2 (ST7789, NOR flash, joystick)",
            "rtos": "FreeRTOS V11.1.0, GCC/ARM_CM4F, heap_4, configUSE_TIMERS=0, tick 1 kHz, max-syscall prio 5",
            "toolchain": "arm-none-eabi-gcc 14.2 (C++14, -fno-exceptions -fno-rtti); host tests: g++",
            "build_system": "CMake presets (firmware-debug/-release, host-tests); scripts/build/build_direct.bat wraps the CMake build",
        },
        "layers": layers,
        "rtos": {"tasks": TASKS},
        "global_nets": global_nets(),
        "harnesses": hs,
        "edges": edges,
        "generated_by": "scripts/arch/gen_architecture.py",
    }


def mermaid(harnesses):
    lines = ["flowchart TB",
             "  classDef layer fill:#eef3f8,stroke:#4a6fa5,stroke-width:1.5px,color:#111",
             "  classDef rtos fill:#fff4e0,stroke:#c8842a,stroke-width:1.5px,color:#111",
             "  classDef harness fill:#eafaea,stroke:#3a8f3a,stroke-width:1.5px,color:#111",
             "  subgraph STACK[Layer stack]", "    direction TB"]
    for lid, name, _, _ in LAYERS[:5]:
        lines.append(f'    {lid}["{lid} {name}"]:::layer')
    lines += ["  end", "  subgraph RTOS[RTOS column: orthogonal]", "    direction TB",
              '    TASKS["Task sheet (Scheduler::)\\nMotor p4 / Comms p3 / Encoder p2 / Display p1"]:::rtos',
              '    PLATFORM["Platform power net\\nclock, us clock, locks, queues, scheduler, notifier"]:::rtos',
              '    TRACE["Trace net (debug builds)\\nharness/trace ITRACE + sink"]:::rtos', "  end",
              '  HARNESS["Harness defs/ + connect()"]:::harness']
    for h in harnesses:
        n = len(h["signals"])
        style = " -. " if h["id"] == "L5_L1" else " -- "
        arrow = " .-> " if h["id"] == "L5_L1" else " --> "
        label = f'H_{h["id"]}: {n} signal(s)' + (" (documented exception)" if h["id"] == "L5_L1" else "")
        frm, to = h["id"].split("_")   # harness ids are "<upper>_<lower>" using the node ids above
        lines.append(f'  {frm}{style}"{label}"{arrow}{to}')
    lines.append('  PLATFORM -- "power net (rule 4)" --> STACK')
    lines.append('  PLATFORM -- "power net (rule 4)" --> TASKS')
    lines.append('  TRACE -. "global net (L2, L3, harness taps)" .-> STACK')
    return "\n".join(lines)


def document(harnesses, manifest):
    out = ["# Architecture", "",
           "Generated by `scripts/arch/gen_architecture.py` from the harness definition tables in",
           "`server/harness/defs/`. Do not edit by hand; edit the tables and regenerate.", "",
           "## Rules", "",
           "1. **Tasks call services.** The task sheet (`F_platform/tasks`) only waits, then calls one L3 entry point. Tasks never touch L1, L2, L4 or L5.",
           "2. **Adjacent layers only.** L1 - L2 - L3 - L4 - L5, each through that boundary's harness. No skipping.",
           "3. **One exception:** L5 -> L1 host command bytes through `Transport::IByteChannel` (owned by L1).",
           "4. **Platform is a power net.** `F_platform/boundary` primitives may be used by any sheet; L3 contains no RTOS loops and is host-testable. `harness/trace/` is the second global net (debug builds only, see below).",
           "5. **The composition root may see every sheet.** `server/main.cpp` + `system_init.cpp` call the Platform, L5 and L3 init contracts and the task-init contracts directly; no other sheet may.", "",
           "Enforced by `scripts/arch/check_includes.py --strict` and `scripts/arch/check_harness_defs.py --strict`.", "",
           "## Layers", ""]
    for l in manifest["layers"]:
        out.append(f'- **{l["id"]} {l["name"]}** â€” {l["responsibility"]} ({len(l["files"])} files)')
    out += ["", "## Global nets", "",
            "Two header sets are wired like power rails: any sheet may include them directly, no harness row describes them.", ""]
    for n in manifest["global_nets"]:
        inc = ", ".join(f"{k} {v}" for k, v in n["direct_includers"].items()) or "none"
        out.append(f'- **{n["id"]} {n["name"]}** (`{n["path"]}`) â€” {n["rule"]}. Direct includers: {inc}.')
    out += ["", "## Harnesses", ""]
    for h in harnesses:
        out += [f'### H_{h["id"]} â€” {h["upper"]} <-> {h["lower"]}', "",
                f'`{h["file"]}` â€” {h["brief"]}', "",
                "| Signal | Provider | Flow | Timing | Context | Ownership |", "|---|---|---|---|---|---|"]
        for s in h["signals"]:
            out.append(f'| `{s["name"]}` | {s["provider"]} | {s["flow"]} | {s["timing"]} | {", ".join(s["ctx"])} | {s["ownership"]} |')
        out.append("")
    out += ["## Tasks", "", "| Task | Priority | Stack (words) | Trigger | Period (ms) | Body |", "|---|---|---|---|---|---|"]
    for t in TASKS:
        out.append(f'| {t["name"]} | {t["priority"]} | {t["stack"]} | {t["trigger"]} | {t["period_ms"]} | {t["body"]} |')
    out += ["", "## Diagram", "", "```mermaid", mermaid(harnesses), "```", ""]
    return "\n".join(out)


def main():
    harnesses = parse_harnesses()
    manifest = build_manifest(harnesses)
    md = document(harnesses, manifest)
    js = json.dumps(manifest, indent=2)
    md_path = os.path.join(DOCS, "ARCHITECTURE.md")
    js_path = os.path.join(DOCS, "architecture_manifest.json")
    if "--check" in sys.argv:
        stale = [p for p, new in ((md_path, md), (js_path, js)) if not os.path.exists(p) or read(p) != new]
        if stale:
            print("stale: " + ", ".join(os.path.relpath(p, ROOT) for p in stale))
            return 1
        print("architecture docs up to date")
        return 0
    with open(md_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(md)
    with open(js_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(js)
    print(f"wrote {os.path.relpath(md_path, ROOT)} and {os.path.relpath(js_path, ROOT)}: "
          f"{len(harnesses)} harnesses, {sum(len(h['signals']) for h in harnesses)} signals")
    return 0


if __name__ == "__main__":
    sys.exit(main())
