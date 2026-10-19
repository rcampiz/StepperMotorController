# Third-party notices

The project's own code is under the MIT License (`LICENSE`). The pieces below come from
others and keep their own licences; each is compatible with use in commercial and closed
products as long as its notice is kept.

## In the firmware image

| Component | Where | Licence |
|---|---|---|
| FreeRTOS Kernel (Amazon) | `server/external/X_middlewares/Third_Party/FreeRTOS-Kernel` (git submodule) | MIT, see its `LICENSE.md` |
| SEGGER RTT | `server/external/X_middlewares/SEGGER/RTT` | SEGGER's BSD-style licence (1 condition: keep the notice), in each file's header |
| SEGGER SystemView (target side) | `server/external/X_middlewares/SEGGER/SystemView` | same SEGGER licence, in each file's header |
| CMSIS Cortex-M4 core header, reduced | `server/external/X_vendor/CMSIS/core_cm4.h` | derived from Arm CMSIS 5 `core_cm4.h`, Apache-2.0 |
| STM32F401xE device header, reduced | `server/external/X_vendor/CMSIS/stm32f401xe.h` | derived from STMicroelectronics STM32CubeF4 `stm32f401xe.h`, Apache-2.0 |
| LCD bitmap fonts | `server/layers/L4_drivers/devices/font_8x12.hpp`, `font_12x16.hpp` | see below |

The two CMSIS headers are hand-reduced subsets (register and bit definitions only) of the
originals; the Apache License 2.0 text is at https://www.apache.org/licenses/LICENSE-2.0.

**LCD fonts:** `scripts/lcd/gen_fonts.py` renders the two glyph tables from a TrueType font.
The tables in the repository were rendered from Microsoft Consolas, which is licensed with
Windows and not for redistribution; they are to be regenerated from an openly licensed font.

## Python client (`client/`), installed separately, not vendored

| Package | Licence |
|---|---|
| pyserial | BSD-3-Clause |
| PySide6 (GUI) | LGPL-3.0 (used as a library, unmodified) |
| pyqtgraph | MIT |
| NumPy | BSD-3-Clause |
| SciPy | BSD-3-Clause |
| Pillow | MIT-CMU (HPND) |
| pylink-square (optional, J-Link RTT) | Apache-2.0 |
| pytest (development) | MIT |
