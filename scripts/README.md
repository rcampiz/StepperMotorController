# scripts/

| Folder | What | Typical call |
|---|---|---|
| `build/` | `env.ps1` (puts the toolchains on PATH for this shell), `build.ps1` (presets: firmware, tests, release), `flash.bat` (build + OpenOCD flash), legacy `build*.bat` wrappers | `. scripts\build\env.ps1` then `cmake --build --preset firmware-debug` |
| `arch/` | architecture checks and generators run by the CMake `arch-check` target: `check_includes.py`, `check_harness_defs.py`, `gen_architecture.py` (ARCHITECTURE.md + manifest), `gen_protocol.py` (PROTOCOL.md, protocol.json, firmware help table, client command spec), `gen_fake_dispatchers.py` (host-test fakes) | `python scripts/arch/gen_protocol.py` after changing an L2 handler |
| `bench/` | hardware bring-up and measurement drivers over the ST-LINK VCP: `bringup_step0.py` (serial bring-up list with PASS/FAIL), `estop_baseline.py` (ESTOP latency scenarios) | `python scripts/bench/bringup_step0.py COM5` |
| `client/` | one-click launchers for the host client on Windows (`.bat`) and Linux/Raspberry Pi (`.sh`); they create `client/.venv` on first use | `scripts\client\run_gui.bat`, `./scripts/client/run_gui.sh --sim` |
| `setup/` | fresh-clone bring-up: `bringup.ps1` (Windows, winget) and `bringup.sh` (Debian / Raspberry Pi OS, apt) install or verify every tool, fetch the FreeRTOS submodule, create the Python venvs, and queue what needs administrator rights or a manual download (ST-LINK driver, winget itself) with the download links; `-OpenLinks` opens them | `powershell -ExecutionPolicy Bypass -File scripts\setup\bringup.ps1`, `./scripts/setup/bringup.sh --install` |
| `debug/` | `debug_openocd.ps1` (OpenOCD launcher used by VS Code), `stlink_to_jlink.ps1` (probe firmware swap) | |

Flashing without the ST-LINK debug driver: copy a `.bin` (`arm-none-eabi-objcopy -O binary`)
to the NUCLEO's `NOD_F401RE` drive; the board programs and resets itself.
