# NUCLEO-F401RE LED Array Project

## Hardware
- **Board**: NUCLEO-F401RE
- **MCU**: STM32F401RET6 (ARM Cortex-M4, 84MHz, 512KB Flash, 96KB RAM)
- **Debug Interface**: ST-LINK/V2-1 (onboard)

## Development Environment Setup

### Required Tools Installation Steps

#### 1. ARM GCC Toolchain (REQUIRED)
   - Download from: https://developer.arm.com/downloads/-/gnu-rm
   - Install the latest version (e.g., 13.2.rel1)
   - During installation:
     - Check "Add path to environment variable"
     - Default install location: `C:\Program Files (x86)\GNU Arm Embedded Toolchain\`
   - Verify installation: Open new terminal and run `arm-none-eabi-gcc --version`

#### 2. Make for Windows (REQUIRED)
   Choose ONE option:

   **Option A: MinGW-w64 with MSYS2 (Recommended)**
   - Download from: https://www.msys2.org/
   - Install and run MSYS2
   - Update package database: `pacman -Syu`
   - Install make: `pacman -S make`
   - Add to PATH: `C:\msys64\usr\bin`

   **Option B: Make for Windows**
   - Download from: http://gnuwin32.sourceforge.net/packages/make.htm
   - Add installation bin folder to PATH

   **Option C: Use Git Bash (if Git is installed)**
   - Git for Windows includes make in Git Bash

#### 3. OpenOCD (REQUIRED for debugging)
   - Download from: https://github.com/xpack-dev-tools/openocd-xpack/releases
   - Extract to `C:\OpenOCD\` or similar
   - Add `C:\OpenOCD\bin` to PATH
   - Verify: `openocd --version`

#### 4. STM32CubeProgrammer (Optional, for advanced flashing)
   - Download: https://www.st.com/en/development-tools/stm32cubeprog.html
   - Only needed if OpenOCD doesn't work

### VSCode Extensions (REQUIRED)
1. **C/C++** (Microsoft) - For C++ IntelliSense
2. **Cortex-Debug** - For ARM debugging with F5 support
   - Install from VSCode marketplace
   - Search for "Cortex-Debug" by marus25

### Verification
After installation, verify all tools in a NEW terminal:
```bash
arm-none-eabi-gcc --version
make --version
openocd --version
```

## Project Structure

```
LedArray/
├── Core/
│   ├── Src/          # Application source files (C++)
│   └── Inc/          # Application header files
├── Drivers/
│   ├── STM32F4xx_HAL_Driver/  # STM32 HAL library
│   ├── CMSIS/                  # ARM CMSIS files
│   └── Custom/                 # Custom peripheral drivers
├── build/            # Build output directory
├── Makefile          # Build configuration
└── README.md         # This file
```

## Building the Project

```bash
make all
```

## Flashing the Board

```bash
make flash
```

## Debugging

Use VSCode with Cortex-Debug extension or:
```bash
make debug
```

## Quick Start Guide

### First Time Setup
1. **Install Tools** (follow instructions above):
   - ARM GCC Toolchain
   - Make
   - OpenOCD

2. **Install VSCode Extensions**:
   - C/C++ (Microsoft)
   - Cortex-Debug

3. **Verify Installation** (open NEW terminal after installation):
   ```bash
   arm-none-eabi-gcc --version
   make --version
   openocd --version
   ```

### Building and Running

#### Option 1: Using VSCode (Easiest)
1. Open this folder in VSCode
2. Connect NUCLEO-F401RE via USB
3. Press **F5** to build, flash, and start debugging
   - First press will build the project
   - OpenOCD will flash the firmware
   - Debugger will stop at main()
4. Press F5 again (or Continue) to run
5. The onboard LED (LD2/PA5) should blink every 500ms

#### Option 2: Using Command Line
1. Connect NUCLEO-F401RE via USB
2. Build: `make all`
3. Flash: `make flash`
4. The LED should start blinking automatically

### Debugging from VSCode
- Press **F5** to start debugging
- Use breakpoints, step through code, inspect variables
- The "Cortex-Debug" configuration provides peripheral register views

## C++ Notes

This project uses C++ (embedded subset):
- No STL (due to memory constraints)
- No exceptions (disabled with -fno-exceptions)
- No RTTI (disabled with -fno-rtti)
- Limited dynamic memory allocation
- C++11/14 features available (constexpr, auto, lambdas, etc.)

## Next Steps

1. Configure peripherals using STM32CubeMX
2. Implement custom drivers for additional hardware
3. Set up debugging configuration in VSCode
