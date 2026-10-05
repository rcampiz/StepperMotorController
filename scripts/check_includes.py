#!/usr/bin/env python3
"""
check_includes.py -- enforce the layering rules (2026-10-04) on #include directives.

Rules (authoritative; see CODE_PHILOSOPHY.md and ARCHITECTURE.md):
  1. Tasks call services. F_platform/tasks (sheet TASKS) may include only its own
     directory, harness/defs/harness_tasks_l3.hpp, shared pins, and Platform.
  2. Adjacent layers only. Ln may include its own directory and the two harness
     definitions that touch it (harness_l(n-1)_l(n), harness_l(n)_l(n+1)).
  3. One exception: L5 -> L1 bytes via harness/defs/harness_l5_l1_bytes.hpp
     (L1 and L5 may include it).
  4. Platform is a power net: F_platform/boundary/ may be included by any sheet.

Everything else (shared PODs in harness/pins/, trace macros in harness/trace/,
harness/defs/harness_signal.hpp, std headers) is allowed everywhere. The composition
root (server/system_init.*, main.cpp) may include every sheet, and
L3_services/ init headers. harness/ itself may include anything.

Usage:
  python scripts/check_includes.py            # report
  python scripts/check_includes.py --strict   # exit 1 on any violation (CI gate)
  python scripts/check_includes.py --baseline N   # exit 1 only if violations > N
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SERVER = os.path.join(ROOT, "server")

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')

# Prefixes that are never a layering concern.
ALWAYS_OK = (
    "harness/pins/", "harness/trace/", "harness/defs/harness_signal.hpp",
    "F_platform/boundary/",            # rule 4: power net
)

# Prefixes that mark an include as "cross-boundary" (only these are judged).
JUDGED_PREFIXES = (
    "L1_transport/", "L2_protocol/", "L3_services/", "L4_drivers/", "L5_board/",
    "F_platform/", "F_util/", "harness/", "X_vendor/", "X_middlewares/",
    "FreeRTOS", "task.h", "queue.h", "semphr.h", "timers.h", "event_groups.h",
    "stm32f4", "core_cm", "SEGGER",
)

EXTERNAL_HW = ("X_vendor/", "X_middlewares/", "FreeRTOS", "task.h", "queue.h",
               "semphr.h", "timers.h", "event_groups.h", "stm32f4", "core_cm", "SEGGER")

ALLOWED = {
    "L1": ("L1_transport/", "harness/defs/harness_l1_l2.hpp", "harness/defs/harness_l5_l1_bytes.hpp"),
    "L2": ("L2_protocol/",  "harness/defs/harness_l1_l2.hpp", "harness/defs/harness_l2_l3.hpp"),
    "L3": ("L3_services/",  "harness/defs/harness_l2_l3.hpp", "harness/defs/harness_l3_l4.hpp"),
    "L4": ("L4_drivers/",   "harness/defs/harness_l3_l4.hpp", "harness/defs/harness_l4_l5.hpp"),
    "L5": ("L5_board/",     "harness/defs/harness_l4_l5.hpp", "harness/defs/harness_l5_l1_bytes.hpp",
           "harness/pins/exti_callbacks.hpp") + EXTERNAL_HW,
    "TASKS":    ("F_platform/tasks/", "harness/defs/harness_tasks_l3.hpp"),
    "PLATFORM": ("F_platform/", "F_util/") + EXTERNAL_HW,
    "SYSTEM":   ("",),   # composition root: may see every sheet (it wires them)
    "HARNESS":  ("",),   # everything
}


def classify(rel):
    p = rel.replace("\\", "/")
    if p.startswith("server/external/"):
        return None
    if p in ("server/main.cpp", "server/system_init.cpp", "server/system_init.hpp"):
        return "SYSTEM"
    if p.startswith("server/harness/"):
        return "HARNESS"
    if p.startswith("server/foundation/F_platform/tasks/"):
        return "TASKS"
    if p.startswith("server/foundation/"):
        return "PLATFORM"
    for n in range(1, 6):
        if p.startswith(f"server/layers/L{n}_"):
            return f"L{n}"
    return None


def judged(inc):
    return any(inc.startswith(pfx) for pfx in JUDGED_PREFIXES)


def allowed(sheet, inc):
    if any(inc.startswith(pfx) for pfx in ALWAYS_OK):
        return True
    return any(inc.startswith(pfx) for pfx in ALLOWED[sheet])


def main():
    strict = "--strict" in sys.argv
    baseline = None
    if "--baseline" in sys.argv:
        baseline = int(sys.argv[sys.argv.index("--baseline") + 1])

    violations = []
    per_sheet = {}
    for dirpath, _, files in os.walk(SERVER):
        for f in files:
            if not f.endswith((".cpp", ".hpp", ".h", ".c")):
                continue
            full = os.path.join(dirpath, f)
            rel = os.path.relpath(full, ROOT).replace("\\", "/")
            sheet = classify(rel)
            if sheet is None or sheet == "HARNESS":
                continue
            with open(full, encoding="utf-8", errors="ignore") as fh:
                for ln, line in enumerate(fh, 1):
                    m = INCLUDE_RE.match(line)
                    if not m:
                        continue
                    inc = m.group(1)
                    if not judged(inc):
                        continue
                    if not allowed(sheet, inc):
                        violations.append((rel, ln, sheet, inc))
                        per_sheet[sheet] = per_sheet.get(sheet, 0) + 1

    for rel, ln, sheet, inc in violations:
        print(f"  {rel}:{ln}: {sheet} may not include '{inc}'")
    print()
    print(f"{len(violations)} violation(s) in {len({v[0] for v in violations})} file(s)."
          + (f"  by sheet: {per_sheet}" if per_sheet else ""))

    if strict and violations:
        return 1
    if baseline is not None and len(violations) > baseline:
        print(f"FAIL: {len(violations)} > baseline {baseline}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
