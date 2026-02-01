# NUCLEO-F401RE Project Setup Instructions

## What's Been Done

Your NUCLEO-F401RE LED blink project is now **fully configured** with:

### Project Files Created
- ✅ **[Core/Src/main.cpp](Core/Src/main.cpp)** - C++ LED blink superloop application
- ✅ **[Core/Src/system_stm32f4xx.c](Core/Src/system_stm32f4xx.c)** - System initialization
- ✅ **[Core/Src/startup_stm32f401xe.s](Core/Src/startup_stm32f401xe.s)** - Startup assembly code
- ✅ **[Drivers/CMSIS/stm32f401xe.h](Drivers/CMSIS/stm32f401xe.h)** - MCU peripheral definitions
- ✅ **[Drivers/CMSIS/core_cm4.h](Drivers/CMSIS/core_cm4.h)** - ARM Cortex-M4 core definitions
- ✅ **[Makefile](Makefile)** - Build system configuration
- ✅ **[STM32F401RETx_FLASH.ld](STM32F401RETx_FLASH.ld)** - Linker script
- ✅ **[.vscode/launch.json](.vscode/launch.json)** - Debug configuration (F5 support)
- ✅ **[.vscode/tasks.json](.vscode/tasks.json)** - Build tasks
- ✅ **[.vscode/c_cpp_properties.json](.vscode/c_cpp_properties.json)** - IntelliSense configuration

### Features
- **C++ Support**: Uses embedded C++14 subset (no STL, no exceptions, no RTTI)
- **LED Class**: Object-oriented LED driver class
- **Superloop Architecture**: Simple infinite loop that blinks LED
- **F5 Debugging**: Press F5 in VSCode to build, flash, and debug
- **Hardware**: Configured for NUCLEO-F401RE with LED on PA5

---

## What You Need to Do

### Step 1: Install Required Tools

#### 1.1 ARM GCC Toolchain
1. Download: https://developer.arm.com/downloads/-/gnu-rm
2. Install (choose latest version, e.g., 13.2.rel1)
3. **IMPORTANT**: Check "Add path to environment variable" during installation
4. Default location: `C:\Program Files (x86)\GNU Arm Embedded Toolchain\`

#### 1.2 Make (Build Tool)
**Choose ONE option:**

**Option A: Using MSYS2 (Recommended)**
1. Download: https://www.msys2.org/
2. Install MSYS2
3. Open MSYS2 terminal and run:
   ```bash
   pacman -Syu          # Update package database
   pacman -S make       # Install make
   ```
4. Add to PATH: `C:\msys64\usr\bin`

**Option B: Using Git Bash (If Git installed)**
- Git for Windows includes make
- Just make sure Git Bash is in your PATH

**Option C: Standalone Make**
- Download: http://gnuwin32.sourceforge.net/packages/make.htm
- Add bin folder to PATH

#### 1.3 OpenOCD (For debugging/flashing)
1. Download: https://github.com/xpack-dev-tools/openocd-xpack/releases
2. Extract to `C:\OpenOCD\` (or your preferred location)
3. Add `C:\OpenOCD\bin` to PATH

#### 1.4 Add Tools to PATH (Windows)
1. Open "Edit the system environment variables"
2. Click "Environment Variables"
3. Under "System variables", select "Path" and click "Edit"
4. Add the following paths (adjust if different):
   - `C:\Program Files (x86)\GNU Arm Embedded Toolchain\13.2.rel1\bin`
   - `C:\msys64\usr\bin` (or your make location)
   - `C:\OpenOCD\bin`
5. Click OK and **restart your terminal/VSCode**

### Step 2: Install VSCode Extensions

1. Open VSCode
2. Go to Extensions (Ctrl+Shift+X)
3. Install:
   - **C/C++** by Microsoft
   - **Cortex-Debug** by marus25

### Step 3: Verify Installation

Open a **NEW** terminal (important for PATH changes) and run:

```bash
arm-none-eabi-gcc --version
make --version
openocd --version
```

All three commands should show version information without errors.

---

## Using the Project

### Quick Start (Using VSCode)

1. **Connect Board**: Plug in your NUCLEO-F401RE via USB
2. **Open Project**: Open this folder in VSCode
3. **Press F5**: This will:
   - Build the project
   - Flash to the board via OpenOCD
   - Start debugging (will stop at `main()`)
4. **Press F5 again** (or click Continue) to run
5. **Watch LED blink**: LD2 (green LED on PA5) should blink every 500ms

### Using Command Line

```bash
# Build the project
make all

# Flash to board
make flash

# Clean build files
make clean
```

### Debugging Tips

- **Breakpoints**: Click left of line numbers in VSCode
- **Step Through**: F10 (step over), F11 (step into)
- **Variables**: Hover over variables to see values
- **Peripheral Registers**: Cortex-Debug shows peripheral registers in sidebar

---

## Troubleshooting

### "command not found" errors
- Make sure tools are installed
- Verify PATH is set correctly
- **Restart terminal/VSCode** after changing PATH

### OpenOCD connection errors
- Make sure NUCLEO board is connected
- Check Windows Device Manager for ST-LINK driver
- May need to install ST-LINK drivers from ST website

### Build errors about missing headers
- Check that ARM GCC is in PATH
- Verify `arm-none-eabi-gcc --version` works

### VSCode IntelliSense issues
- Open Command Palette (Ctrl+Shift+P)
- Run "C/C++: Edit Configurations (UI)"
- Verify compiler path points to `arm-none-eabi-gcc.exe`

---

## Project Details

### LED Blink Implementation

The project uses a simple C++ class to control the LED:

```cpp
LED userLED(GPIOA, 5);  // PA5 = LD2 on NUCLEO-F401RE

while (true) {
    userLED.on();
    delay_ms(500);

    userLED.off();
    delay_ms(500);
}
```

### Hardware Details
- **Board**: NUCLEO-F401RE
- **MCU**: STM32F401RET6 (ARM Cortex-M4)
- **Clock**: 16MHz HSI (internal oscillator)
- **LED**: PA5 (LD2, green user LED)
- **Debug**: ST-LINK/V2-1 (onboard)

### C++ Embedded Subset
This project uses embedded C++ with:
- ✅ Classes and objects
- ✅ C++14 features (auto, constexpr, lambdas)
- ✅ Templates (if needed)
- ❌ No STL (too large for embedded)
- ❌ No exceptions (`-fno-exceptions`)
- ❌ No RTTI (`-fno-rtti`)
- ❌ Minimal heap usage

---

## Next Steps

Once the LED is blinking, you can:

1. **Add more peripherals**: UART, SPI, I2C, Timers
2. **Create additional drivers**: Similar to the LED class
3. **Implement LED patterns**: Different blink rates, patterns
4. **Add external hardware**: Sensors, displays, etc.
5. **Optimize performance**: Configure PLL for 84MHz operation

Happy coding! Press F5 when ready to test.