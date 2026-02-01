# FreeRTOS Task Architecture

This document describes the FreeRTOS task design for the Stepper Motor Controller.

## Task Overview

| Task | Priority | Stack | Period | Purpose |
|------|----------|-------|--------|---------|
| MotorTask | 4 (highest) | 256 words | Event-driven | Motor command processing |
| CommsTask | 3 | 512 words | 10ms poll | Command parsing, telemetry |
| EncoderTask | 2 | 128 words | 10ms | Encoder sampling (100 Hz) |
| DisplayTask | 1 (lowest) | 256 words | 100ms | LCD refresh (10 Hz) |

## Priority Rationale

1. **MotorTask (Priority 4)** - Highest priority because motor commands are time-critical. Missing steps or delayed responses could cause position errors or missed limit switches.

2. **CommsTask (Priority 3)** - High priority to ensure responsive command handling. Commands from Raspberry Pi should be processed quickly.

3. **EncoderTask (Priority 2)** - Medium priority for consistent sampling. 100 Hz sampling captures position and velocity accurately.

4. **DisplayTask (Priority 1)** - Lowest priority since LCD updates are purely informational and can be delayed without affecting system operation.

## Task Details

### MotorTask

**File:** `Core/Inc/tasks/motor_task.hpp`, `Core/Src/tasks/motor_task.cpp`

**Purpose:** Processes motor commands received via queue and drives the powerSTEP01.

**Interface:**
```cpp
namespace Tasks {
    bool MotorTask_Init();                    // Initialize queue and driver
    void vMotorTask(void* pvParameters);      // Task entry point
    bool MotorTask_SendCommand(const MotorCommand& cmd, TickType_t timeout = 0);
    bool MotorTask_Move(int32_t steps);       // Convenience function
    bool MotorTask_Stop(bool hard = false);   // Convenience function

    extern QueueHandle_t g_motorCmdQueue;     // Command queue handle
}
```

**Command Types:**
```cpp
enum class MotorCmdType : uint8_t {
    Move,           // Relative move
    GoTo,           // Absolute move
    Run,            // Continuous rotation
    SoftStop,       // Decelerate to stop
    HardStop,       // Immediate stop
    SoftHiZ,        // Decelerate then Hi-Z
    HardHiZ,        // Immediate Hi-Z
    GoHome,         // Return to home
    GoMark,         // Go to mark position
    ResetPos,       // Zero current position
    SetAccel,       // Configure acceleration
    SetDecel,       // Configure deceleration
    SetMaxSpeed,    // Configure max speed
    SetMark,        // Set mark position
    GetStatus       // Force status update
};

struct MotorCommand {
    MotorCmdType type;
    int32_t param1;   // Steps, position, or speed
    int32_t param2;   // Direction for RUN, etc.
};
```

**Behavior:**
- Blocks on queue receive with 50ms timeout
- Processes command immediately when received
- Updates telemetry after each command
- Periodic status polling every 50ms

### EncoderTask

**File:** `Core/Inc/tasks/encoder_task.hpp`, `Core/Src/tasks/encoder_task.cpp`

**Purpose:** Reads quadrature encoder using TIM2 hardware encoder mode.

**Interface:**
```cpp
namespace Tasks {
    bool EncoderTask_Init();                  // Configure TIM2 and EXTI
    void vEncoderTask(void* pvParameters);    // Task entry point
    EncoderState EncoderTask_GetState();      // Get thread-safe copy
    int32_t EncoderTask_GetCount();           // Fast direct read
    void EncoderTask_ClearIndexFlag();        // Clear index seen
    void EncoderTask_ResetCount();            // Zero counter
    void EncoderTask_IndexISR();              // ISR callback
}
```

**Hardware Configuration:**
- TIM2 in encoder mode 3 (count on both edges)
- PA0 = TIM2_CH1 (Encoder A)
- PA1 = TIM2_CH2 (Encoder B)
- PC4 = EXTI4 (Index pulse Z)

**Behavior:**
- Samples TIM2->CNT every 10ms
- Calculates velocity from delta
- Detects index pulse via EXTI4
- Updates telemetry each cycle

**Index Pulse Handling:**
```cpp
// In stm32f4xx_it.c or encoder_task.cpp
extern "C" void EXTI4_IRQHandler(void) {
    if (EXTI->PR & EXTI_PR_PR4) {
        EXTI->PR = EXTI_PR_PR4;  // Clear pending
        Tasks::EncoderTask_IndexISR();
    }
}
```

### DisplayTask

**File:** `Core/Inc/tasks/display_task.hpp`, `Core/Src/tasks/display_task.cpp`

**Purpose:** Refreshes LCD with telemetry data and handles joystick navigation.

**Interface:**
```cpp
namespace Tasks {
    bool DisplayTask_Init();                  // Initialize LCD
    void vDisplayTask(void* pvParameters);    // Task entry point
    void DisplayTask_SetPage(DisplayPage page);
    DisplayPage DisplayTask_GetPage();
    void DisplayTask_Refresh();               // Force refresh
}

enum class DisplayPage : uint8_t {
    Status,         // Main status screen
    MotorDetail,    // Motor details
    EncoderDetail,  // Encoder details
    System,         // System info
    Debug           // Debug output
};
```

**Behavior:**
- Polls joystick for page navigation
- Left/Right = previous/next page
- Center = force refresh
- Reads telemetry snapshot each cycle
- Renders current page to LCD

### CommsTask

**File:** `Core/Inc/tasks/comms_task.hpp`, `Core/Src/tasks/comms_task.cpp`

**Purpose:** Handles communication with Raspberry Pi.

**Interface:**
```cpp
namespace Tasks {
    bool CommsTask_Init(TransportType transport = TransportType::VCP_UART);
    void vCommsTask(void* pvParameters);
    void CommsTask_EnableTelemetry(bool enable);
    bool CommsTask_IsTelemetryEnabled();
}

enum class TransportType : uint8_t {
    VCP_UART,   // USART2 via ST-LINK VCP
    RTT         // SEGGER RTT channel 0
};
```

**Behavior:**
- Initializes selected transport
- Polls for incoming commands every 10ms
- Parses commands via `CommandParser`
- Dispatches motor commands to queue
- Publishes telemetry every 100ms (if enabled)

## Inter-Task Communication

### Queue: motorCmdQueue

```
CommsTask ------> motorCmdQueue ------> MotorTask
                  (depth: 8)
```

- **Producer:** CommsTask (from parsed commands)
- **Consumer:** MotorTask
- **Item size:** sizeof(MotorCommand) (typically 12-16 bytes with ARM alignment)
- **Depth:** 8 commands

### Shared State: TelemetryManager

```
MotorTask ------+
                |
EncoderTask ----+------> TelemetryManager <------ CommsTask
                |        (mutex protected)        DisplayTask
SystemTask -----+
```

- **Producers:** MotorTask, EncoderTask, SystemTask
- **Consumers:** CommsTask (for publishing), DisplayTask (for rendering)
- **Protection:** FreeRTOS mutex with 10ms timeout

### Direct Register Access

- `EncoderTask_GetCount()` - Direct TIM2->CNT read (atomic on Cortex-M4, no mutex needed)

## Initialization Sequence

```cpp
int main(void) {
    // 1. Hardware initialization
    HAL_Init();
    SystemClock_Config();
    SPI_Init();

    // 2. SEGGER SystemView (optional)
    // SEGGER_SYSVIEW_Conf();

    // 3. Task initialization (order matters!)
    Comms::g_telemetry.init();        // Telemetry first
    Tasks::EncoderTask_Init();         // No dependencies
    Tasks::MotorTask_Init();           // Creates queue
    Tasks::DisplayTask_Init();         // Uses drivers
    Tasks::CommsTask_Init(Tasks::TransportType::VCP_UART);

    // 4. Create tasks
    xTaskCreate(Tasks::vEncoderTask, "Encoder", 128, NULL, 2, NULL);
    xTaskCreate(Tasks::vMotorTask, "Motor", 256, NULL, 4, NULL);
    xTaskCreate(Tasks::vDisplayTask, "Display", 256, NULL, 1, NULL);
    xTaskCreate(Tasks::vCommsTask, "Comms", 512, NULL, 3, NULL);

    // 5. Start scheduler
    // SEGGER_SYSVIEW_Start();
    vTaskStartScheduler();

    for(;;) {}  // Never reached
}
```

## Memory Usage

### Stack Estimates

| Task | Stack (words) | Stack (bytes) | Notes |
|------|---------------|---------------|-------|
| MotorTask | 256 | 1024 | SPI transactions |
| EncoderTask | 128 | 512 | Minimal processing |
| DisplayTask | 256 | 1024 | LCD buffer operations |
| CommsTask | 512 | 2048 | Command parsing buffers |
| **Total** | **1152** | **4608** | + kernel overhead |

### Heap Usage

From `FreeRTOSConfig.h`:
```c
#define configTOTAL_HEAP_SIZE   ( ( size_t ) ( 10 * 1024 ) )
```

Approximate breakdown:
- Task stacks: ~5 KB
- Queues: ~100 bytes
- Mutexes: ~80 bytes each
- **Available:** ~4-5 KB for dynamic allocations

## Implementation Status

| Task | Init | Entry | Command Handling | Telemetry |
|------|------|-------|------------------|-----------|
| MotorTask | Stub | Stub | Stub | Stub |
| EncoderTask | Implemented | Implemented | N/A | Implemented |
| DisplayTask | Stub | Stub | N/A | Reads |
| CommsTask | Implemented | Stub | Stub | Stub |
