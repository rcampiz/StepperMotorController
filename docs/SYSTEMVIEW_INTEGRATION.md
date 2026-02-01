# SEGGER SystemView Integration

This document describes the SEGGER SystemView integration for real-time FreeRTOS visualization.

## Status

**Integration Complete** - SEGGER RTT and SystemView are fully integrated and building.

Build output (with SEGGER enabled):
```
   text	   data	    bss	    dec	    hex	filename
  33296	    104	  15700	  49100	   bfcc	StepperMotorController.elf
```

## Overview

SystemView is a real-time recording and visualization tool that shows:
- Task execution timing
- Task switches and preemptions
- ISR execution
- Kernel API calls
- Custom events

It uses SEGGER RTT (Real-Time Transfer) to stream data from the target to the host with minimal overhead.

## Requirements

### Hardware

1. **J-Link Debugger** - SystemView requires J-Link for RTT communication
   - On NUCLEO boards: Convert ST-LINK to J-Link OB
   - Or use external J-Link probe

### Software Downloads

1. **J-Link Software Pack**
   - URL: https://www.segger.com/downloads/jlink/
   - Contains: J-Link drivers, RTT sources, utilities

2. **SystemView**
   - URL: https://www.segger.com/downloads/systemview/
   - Contains: Host application, target sources

### FreeRTOS Version Compatibility

**Important:** This project uses FreeRTOS V11.1.0+. You must use the SystemView FreeRTOS files from the `FreeRTOSV11` folder in the SystemView download, not the default `FreeRTOSV10` files.

## ST-LINK to J-Link Conversion

### Prerequisites

- Download STLinkReflash utility from SEGGER
- Close all ST-LINK software
- Board powered only via USB

### Conversion Steps

1. Run `STLinkReflash.exe`
2. Select "Upgrade to J-Link"
3. Accept license (non-commercial/educational use)
4. Wait for reflash (~30 seconds)
5. Disconnect and reconnect USB

### Verification

```bash
# Should detect STM32F401RE
JLinkExe -device STM32F401RE -if SWD -speed 4000
```

### Rollback

If needed, SEGGER provides a rollback option in STLinkReflash to restore original ST-LINK firmware.

### Licensing Note

J-Link OB (On-Board) is limited to:
- Non-commercial use
- Educational use
- Evaluation purposes

Commercial use requires a full J-Link probe.

## File Installation

### RTT Sources

Copy from RTT download to `Middlewares/SEGGER/RTT/`:
```
SEGGER_RTT.c
SEGGER_RTT.h
SEGGER_RTT_Conf.h
SEGGER_RTT_printf.c
```

**Note:** The assembly file `SEGGER_RTT_ASM_ARMv7M.S` is NOT required. The project uses `RTT_USE_ASM=0` to use the pure C implementation, which avoids the need for platform-specific assembly.

### SystemView Sources

Copy from SystemView download to `Middlewares/SEGGER/SystemView/`:
```
SEGGER_SYSVIEW.c
SEGGER_SYSVIEW.h
SEGGER_SYSVIEW_ConfDefaults.h
SEGGER_SYSVIEW_Int.h
```

**Critical:** For FreeRTOS files, use the `FreeRTOSV11` subfolder:
```
Sample/FreeRTOSV11/SEGGER_SYSVIEW_FreeRTOS.c  ->  Middlewares/SEGGER/SystemView/
Sample/FreeRTOSV11/SEGGER_SYSVIEW_FreeRTOS.h  ->  Middlewares/SEGGER/SystemView/
```

Do NOT use files from `FreeRTOSV10` - they are incompatible with FreeRTOS V11.1.0+.

**Note:** `SEGGER_SYSVIEW_Conf.h` and `SEGGER_SYSVIEW_Config_FreeRTOS.c` are already provided with project-specific configuration.

## Configuration

### FreeRTOSConfig.h

The following settings have been added:

```c
// Required for SystemView
#define configUSE_TRACE_FACILITY                1
#define configUSE_APPLICATION_TASK_TAG          1
#define configRECORD_STACK_HIGH_ADDRESS         1

// Additional INCLUDE macros
#define INCLUDE_xTaskGetIdleTaskHandle          1
#define INCLUDE_pxTaskGetStackStart             1

// Conditional SystemView include
#ifdef ENABLE_SEGGER_SYSTEMVIEW
  #include "SEGGER_SYSVIEW_FreeRTOS.h"
#endif
```

### SEGGER_SYSVIEW_Conf.h

Key settings:
```c
#define SEGGER_SYSVIEW_RTT_CHANNEL              1       // RTT channel (0 = console)
#define SEGGER_SYSVIEW_CPU_FREQ                 configCPU_CLOCK_HZ
#define SEGGER_SYSVIEW_RAM_BASE                 0x20000000
#define SEGGER_SYSVIEW_GET_TIMESTAMP()          (DWT->CYCCNT)
#define SEGGER_SYSVIEW_APP_NAME                 "Stepper Motor Controller"
#define SEGGER_SYSVIEW_DEVICE_NAME              "STM32F401RE"
```

### CMakeLists.txt

The following is already configured in CMakeLists.txt:

```cmake
# Include directories
include_directories(
    ...
    Middlewares/SEGGER/RTT
    Middlewares/SEGGER/SystemView
)

# Defines
add_compile_definitions(
    ...
    ENABLE_SEGGER_SYSTEMVIEW
    RTT_USE_ASM=0              # Use pure C implementation (no assembly required)
)

# Sources (defined BEFORE C_SOURCES to ensure proper ordering)
set(SEGGER_RTT_SOURCES
    Middlewares/SEGGER/RTT/SEGGER_RTT.c
    Middlewares/SEGGER/RTT/SEGGER_RTT_printf.c
)
set(SEGGER_SYSVIEW_SOURCES
    Middlewares/SEGGER/SystemView/SEGGER_SYSVIEW.c
    Middlewares/SEGGER/SystemView/SEGGER_SYSVIEW_Config_FreeRTOS.c
    Middlewares/SEGGER/SystemView/SEGGER_SYSVIEW_FreeRTOS.c
)
```

**Important:** The `RTT_USE_ASM=0` define is required because the default GCC/Cortex-M4 configuration enables assembly optimizations that require an additional `.S` file. Setting this to 0 uses the pure C fallback implementation.

## Initialization

Add to `main.cpp` before starting the scheduler:

```cpp
#ifdef ENABLE_SEGGER_SYSTEMVIEW
#include "SEGGER_SYSVIEW.h"
#endif

int main(void) {
    // ... hardware init ...

    #ifdef ENABLE_SEGGER_SYSTEMVIEW
    SEGGER_SYSVIEW_Conf();
    #endif

    // ... create tasks ...

    #ifdef ENABLE_SEGGER_SYSTEMVIEW
    SEGGER_SYSVIEW_Start();
    #endif

    vTaskStartScheduler();
}
```

## RTT Channel Allocation

| Channel | Direction | Purpose |
|---------|-----------|---------|
| 0 | Up | Console output (printf) |
| 0 | Down | Console input (commands) |
| 1 | Up | SystemView data |

This allows simultaneous console I/O and SystemView tracing.

## Using SystemView

### Starting a Recording

1. Launch SystemView application
2. Target -> Recorder Configuration:
   - Connection: J-Link
   - Target Device: STM32F401RE
   - Target Interface: SWD
   - Interface Speed: 4000 kHz
3. Click "Start Recording"

### Viewing Data

SystemView displays:
- **Timeline** - Task execution over time
- **Task List** - All tasks with statistics
- **Events** - Kernel API calls, ISRs, custom events
- **CPU Load** - Per-task CPU utilization
- **Context Switches** - Count and timing

### Custom Events

Add application-specific markers:

```cpp
#include "SEGGER_SYSVIEW.h"

void someFunction() {
    SEGGER_SYSVIEW_RecordEnterISR();    // Mark ISR entry
    // ... ISR code ...
    SEGGER_SYSVIEW_RecordExitISR();     // Mark ISR exit
}

void applicationEvent() {
    SEGGER_SYSVIEW_PrintfTarget("Motor moved to %d", position);
}
```

## Troubleshooting

### "Cannot connect to target"

- Verify J-Link conversion completed
- Check SWD connection
- Ensure no other debugger is connected
- Try lower speed (1000 kHz)

### "No SystemView data"

- Verify `ENABLE_SEGGER_SYSTEMVIEW` is defined
- Check `SEGGER_SYSVIEW_Start()` is called
- Confirm RTT channel 1 is configured
- Verify DWT cycle counter is enabled

### "Timestamps incorrect"

DWT cycle counter must be enabled:
```c
CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
DWT->CYCCNT = 0;
DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
```

This is done in `SEGGER_SYSVIEW_Conf()`.

### High CPU overhead

- Reduce event frequency
- Increase RTT buffer size
- Use higher SWD speed

## References

- [SEGGER SystemView User Guide](https://www.segger.com/downloads/systemview/UM08027)
- [SEGGER RTT Documentation](https://www.segger.com/products/debug-probes/j-link/technology/about-real-time-transfer/)
- [FreeRTOS Trace Hooks](https://www.freertos.org/rtos-trace-macros.html)
- [ST-LINK On-Board](https://www.segger.com/products/debug-probes/j-link/models/other-j-links/st-link-on-board/)
