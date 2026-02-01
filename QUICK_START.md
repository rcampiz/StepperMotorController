# Quick Start - Get Building in 5 Minutes!

## Current Status

✅ **Project is fully configured**
❌ **ARM GCC Toolchain not installed** ← **You need this first!**
✅ **CMake is installed**
❌ **OpenOCD not installed** (needed for F5 debugging)

---

## Step 1: Install ARM GCC Toolchain (REQUIRED)

This is the compiler that turns your C++ code into firmware for the STM32.

### Windows Installation:

1. **Download ARM GCC**:
   - Go to: https://developer.arm.com/downloads/-/gnu-rm
   - Download the latest Windows installer (e.g., `gcc-arm-none-eabi-13-2-Rel1-mingw-w64-i686-arm-none-eabi.exe`)

2. **Install**:
   - Run the installer
   - **IMPORTANT**: Check the box "Add path to environment variable"
   - Install to default location: `C:\Program Files (x86)\GNU Arm Embedded Toolchain\`

3. **Verify Installation**:
   - Open a **NEW** Command Prompt or Git Bash
   - Run: `arm-none-eabi-gcc --version`
   - You should see version information

### If the PATH wasn't added automatically:

1. Open "Edit the system environment variables"
2. Click "Environment Variables"
3. Under "System variables", select "Path" and click "Edit"
4. Click "New" and add: `C:\Program Files (x86)\GNU Arm Embedded Toolchain\13.2 Rel1\bin` (adjust version number)
5. Click OK, OK, OK
6. **Restart VSCode** and open a new terminal

---

## Step 2: Test the Build

Once ARM GCC is installed:

### Option A: Using VSCode (Easiest)
1. Press `Ctrl+Shift+B` (Build task)
2. Watch the build output
3. If successful, you'll see `Build completed successfully!`

### Option B: Using Command Line
```bash
# From project root
./build.bat

# Or on Linux/Mac
cmake -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

### Expected Output:
```
====================================
NUCLEO-F401RE Build Script
====================================

[1/3] Configuring with CMake...
[2/3] Building project...
[  50%] Building C object CMakeFiles/LedArray.elf.dir/Core/Src/system_stm32f4xx.c.obj
[ 100%] Building CXX object CMakeFiles/LedArray.elf.dir/Core/Src/main.cpp.obj
[ 100%] Linking CXX executable LedArray.elf

====================================
Build completed successfully!
====================================

Output files in build/:
  - LedArray.elf  (ELF executable)
  - LedArray.hex  (Intel HEX format)
  - LedArray.bin  (Binary format)
```

---

## Step 3: Install OpenOCD (For F5 Debugging)

OpenOCD lets you flash and debug the board.

1. **Download**: https://github.com/xpack-dev-tools/openocd-xpack/releases
2. **Extract** to `C:\OpenOCD\`
3. **Add to PATH**: `C:\OpenOCD\bin`
4. **Verify**: `openocd --version`

---

## Step 4: Install Clangd (For Better IntelliSense)

1. Open VSCode
2. Go to Extensions (Ctrl+Shift+X)
3. Search for "clangd"
4. Install **"clangd" by LLVM Extensions**
5. Reload VSCode window (Ctrl+Shift+P → "Developer: Reload Window")

---

## Step 5: Press F5 to Debug!

Once everything is installed:

1. **Connect** your NUCLEO-F401RE board via USB
2. **Press F5** in VSCode
3. The project will:
   - Build automatically
   - Flash to the board via OpenOCD
   - Start debugging (stops at `main()`)
4. **Press F5 again** (or Continue) to run
5. **Watch the LED blink!** (LD2/PA5, green LED, 500ms on/off)

---

## Build System Overview

This project now uses **CMake** instead of Make (since make isn't available):

- **[CMakeLists.txt](CMakeLists.txt)** - Main build configuration
- **[build.bat](build.bat)** - Windows build script (uses CMake + NMake)
- **Makefile** - Still present but not used (for reference)

### Build Commands:

```bash
# Build
./build.bat

# Clean
./build.bat clean

# Build and flash
./build.bat flash

# Or use VSCode tasks
Ctrl+Shift+B           # Build
Ctrl+Shift+P → Flash   # Flash to board
```

---

## Project Structure

```
LedArray/
├── Core/
│   ├── Src/
│   │   ├── main.cpp                  ← Your main application (LED blink)
│   │   ├── system_stm32f4xx.c        ← System initialization
│   │   └── startup_stm32f401xe.s     ← Startup code (vector table)
│   └── Inc/                          ← Your header files (empty for now)
├── Drivers/
│   └── CMSIS/
│       ├── stm32f401xe.h             ← STM32F401 peripheral definitions
│       └── core_cm4.h                ← ARM Cortex-M4 core
├── build/                            ← Build output (created by CMake)
│   ├── LedArray.elf                  ← Executable
│   ├── LedArray.hex                  ← Hex file for flashing
│   ├── LedArray.bin                  ← Binary file
│   └── compile_commands.json         ← For clangd IntelliSense
├── .vscode/
│   ├── launch.json                   ← F5 debugging configuration
│   ├── tasks.json                    ← Build/Flash tasks
│   ├── settings.json                 ← Clangd configuration
│   └── extensions.json               ← Recommended extensions
├── CMakeLists.txt                    ← CMake build configuration
├── build.bat                         ← Windows build script
├── Makefile                          ← Alternative (requires make)
├── STM32F401RETx_FLASH.ld            ← Linker script
└── README.md                         ← Full documentation
```

---

## Troubleshooting

### "arm-none-eabi-gcc: command not found"
- ARM GCC toolchain not installed or not in PATH
- Install ARM GCC (see Step 1)
- Make sure to check "Add path to environment variable" during installation
- Restart terminal/VSCode after installation

### "cmake: command not found"
- CMake is installed but not in Git Bash PATH
- Use Command Prompt instead: `build.bat`
- Or run from VSCode: Press Ctrl+Shift+B

### Build succeeds but can't flash
- OpenOCD not installed (see Step 3)
- Board not connected via USB
- Wrong USB cable (needs data pins, not just power)
- ST-LINK drivers not installed (Windows Device Manager should show "STMicroelectronics STLink dongle")

### Clangd not working
- Install the clangd extension (see Step 4)
- Build the project first (generates compile_commands.json)
- Reload VSCode window
- Check status bar for "clangd" indicator

---

## What the Code Does

The [main.cpp](Core/Src/main.cpp) file contains a simple LED blink program:

```cpp
class LED {
    // GPIO port and pin number
    GPIO_TypeDef* port;
    uint8_t pin;

public:
    LED(GPIO_TypeDef* gpio_port, uint8_t gpio_pin);
    void init();     // Configure GPIO as output
    void on();       // Turn LED on
    void off();      // Turn LED off
    void toggle();   // Toggle LED state
};

int main() {
    // Initialize LED on PA5 (LD2 - User LED)
    LED userLED(GPIOA, 5);

    // Superloop - blink forever
    while (true) {
        userLED.on();
        delay_ms(500);  // 500ms on

        userLED.off();
        delay_ms(500);  // 500ms off
    }
}
```

This is a **bare-metal C++** program:
- ✅ Uses C++ classes
- ✅ Direct hardware register access
- ✅ No operating system (RTOS)
- ✅ Runs in a simple infinite loop (superloop)
- ❌ No STL (standard library)
- ❌ No exceptions
- ❌ No RTTI

---

## Next Steps After LED Blinks

1. **Try modifying the blink rate** in [main.cpp](Core/Src/main.cpp)
2. **Add more LEDs** on other GPIO pins
3. **Create additional classes** for other peripherals (UART, SPI, etc.)
4. **Explore debugging features** (breakpoints, step through, watch variables)

---

## Quick Reference

| Task | Command | Keyboard Shortcut |
|------|---------|-------------------|
| Build | `build.bat` | Ctrl+Shift+B |
| Clean | `build.bat clean` | - |
| Flash | `build.bat flash` | - |
| Debug | - | F5 |
| Stop Debugging | - | Shift+F5 |

---

**Ready to start? Install ARM GCC (Step 1) and press Ctrl+Shift+B to build!**