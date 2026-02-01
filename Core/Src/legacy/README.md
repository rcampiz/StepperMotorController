# Legacy Files

These files are from the original LED array demo project and are **not built** by the current CMakeLists.txt.

They are preserved for reference but are not part of the stepper motor controller project.

## Files

| File | Original Purpose |
|------|------------------|
| `heartbeat_task.cpp` | LED heartbeat indicator |
| `button_monitor_task.cpp` | User button handling |
| `usb_comm_task.cpp` | USB CDC communication |
| `led_pattern_task.cpp` | LED pattern generation |

## Status

These files are **deprecated** and may be removed in a future cleanup.

If you need similar functionality, refer to the new task implementations in `Core/Src/tasks/`.
