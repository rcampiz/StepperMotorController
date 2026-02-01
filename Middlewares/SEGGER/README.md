# SEGGER Integration

This directory contains configuration files for SEGGER RTT and SystemView.
You must download the actual SEGGER source files separately.

## Required Downloads

### 1. J-Link Software Pack (includes RTT)
- URL: https://www.segger.com/downloads/jlink/
- Extract RTT sources to: `RTT/`

### 2. SystemView
- URL: https://www.segger.com/downloads/systemview/
- Extract SystemView sources to: `SystemView/`

## Required Files

### RTT/ Directory
After downloading, copy these files from the J-Link package:
```
RTT/
├── SEGGER_RTT.c
├── SEGGER_RTT.h
├── SEGGER_RTT_Conf.h      (use default or customize)
├── SEGGER_RTT_printf.c
└── SEGGER_RTT_ASM_ARMv7M.S (optional, for performance)
```

### SystemView/ Directory
After downloading, copy these files from the SystemView package:
```
SystemView/
├── SEGGER_SYSVIEW.c
├── SEGGER_SYSVIEW.h
├── SEGGER_SYSVIEW_Conf.h          (PROVIDED - project-specific config)
├── SEGGER_SYSVIEW_ConfDefaults.h
├── SEGGER_SYSVIEW_Int.h
├── SEGGER_SYSVIEW_FreeRTOS.c
├── SEGGER_SYSVIEW_FreeRTOS.h
└── SEGGER_SYSVIEW_Config_FreeRTOS.c (PROVIDED - callback implementations)
```

**Note:** `SEGGER_SYSVIEW_Conf.h` and `SEGGER_SYSVIEW_Config_FreeRTOS.c` are
already provided with project-specific configuration. Compare with SEGGER's
templates and merge if needed.

## Enabling SystemView

1. Download and copy SEGGER files as described above
2. Uncomment SEGGER lines in `CMakeLists.txt`:
   ```cmake
   set(SEGGER_RTT_SOURCES ...)
   set(SEGGER_SYSVIEW_SOURCES ...)
   ```
3. Add `-DENABLE_SEGGER_SYSTEMVIEW` to compiler flags in CMakeLists.txt
4. Rebuild the project

## RTT Channel Allocation

| Channel | Direction | Purpose |
|---------|-----------|---------|
| 0 | Up/Down | Console I/O (commands/responses) |
| 1 | Up | SystemView events |

## ST-LINK to J-Link Conversion

SystemView requires a J-Link debugger. To convert the on-board ST-LINK:

1. Download STLinkReflash from: https://www.segger.com/products/debug-probes/j-link/models/other-j-links/st-link-on-board/
2. Run STLinkReflash.exe and select "Upgrade to J-Link"
3. Accept license terms (non-commercial/educational use)
4. Reconnect USB after conversion

**Note:** J-Link OB is limited to educational/non-commercial use.
SEGGER provides a rollback option if needed.