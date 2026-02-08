# Requirements: ST-LINK / J-Link OB Debugger Switching

This document specifies requirements for switching between ST-LINK and J-Link OB firmware on the NUCLEO-F401RE on-board debugger.

## Overview

The NUCLEO-F401RE includes an on-board ST-LINK/V2-1 debugger connected via USB. SEGGER provides a utility to convert this debugger to J-Link OB firmware, enabling:
- SEGGER RTT for high-speed bidirectional communication
- SEGGER SystemView for real-time tracing
- J-Link debugging tools

The conversion is **reversible** - ST-LINK firmware can be restored at any time.

## Capability Confirmation

### Can We Program with J-Link OB?

**Yes.** Based on official documentation:

1. **SEGGER ST-LINK On-Board Conversion**
   - SEGGER's STLinkReflash utility converts on-board ST-LINK to J-Link OB
   - The same utility provides "Restore ST-Link Firmware" functionality
   - Source: [SEGGER ST-LINK On-Board](https://www.segger.com/products/debug-probes/j-link/models/other-j-links/st-link-on-board/)

2. **J-Link STM32 Support**
   - J-Link supports all STM32 devices for flash programming and debugging via SWD
   - Source: [SEGGER STM32 Knowledge Base](https://wiki.segger.com/STM32)

3. **Conclusion**
   - Programming/debugging is fully supported with J-Link OB
   - ST-LINK is NOT required for programming
   - However, a reversible workflow is maintained for toolchain compatibility

## Required Scripts

### Windows Scripts

| Script | Purpose |
|--------|---------|
| `scripts/windows/convert_to_jlink.ps1` | Convert ST-LINK to J-Link OB |
| `scripts/windows/restore_stlink.ps1` | Restore original ST-LINK firmware |

### Linux Scripts

| Script | Purpose |
|--------|---------|
| `scripts/linux/convert_to_jlink.sh` | Convert ST-LINK to J-Link OB |
| `scripts/linux/restore_stlink.sh` | Restore original ST-LINK firmware |

## Script Requirements

### Functional Requirements

1. **REQ-DEB-001: Non-Interactive Mode**
   - Scripts MUST support non-interactive execution where possible
   - If interactive prompts are unavoidable, document the expected inputs

2. **REQ-DEB-002: Clear Output**
   - Scripts MUST provide clear console output indicating progress
   - Scripts MUST detect and report success or failure
   - Scripts MUST provide next-steps guidance on completion

3. **REQ-DEB-003: Dependency Checks**
   - Scripts MUST verify required tools are installed before proceeding:
     - SEGGER J-Link Software Pack
     - STLinkReflash utility (typically at `<JLink_Install>/STLinkReflash.exe`)
   - Scripts MUST verify device is connected via USB
   - Scripts MUST report missing dependencies with installation guidance

4. **REQ-DEB-004: Admin Permissions**
   - Scripts MUST document any administrator/root permissions required
   - Scripts SHOULD request elevation if needed (Windows: Run as Administrator)

### STLinkReflash Tool Location

| Platform | Typical Path |
|----------|--------------|
| Windows | `C:\Program Files\SEGGER\JLink\STLinkReflash.exe` |
| Linux | `/opt/SEGGER/JLink/STLinkReflash` |

### Script Behavior

#### convert_to_jlink Script

```
1. Check: J-Link Software Pack installed
2. Check: STLinkReflash utility exists
3. Check: Device connected (ST-LINK detected)
4. Prompt: Confirm conversion (unless --force flag)
5. Execute: STLinkReflash conversion
6. Verify: J-Link OB now detected
7. Output: Success message with next steps
```

#### restore_stlink Script

```
1. Check: J-Link Software Pack installed
2. Check: STLinkReflash utility exists
3. Check: Device connected (J-Link OB detected)
4. Prompt: Confirm restoration (unless --force flag)
5. Execute: STLinkReflash restore
6. Verify: ST-LINK now detected
7. Output: Success message with next steps
```

## VS Code Integration

### Debug Configurations

The project MUST provide `.vscode/launch.json` configurations for both debugger modes:

1. **ST-LINK Debug Configuration**
   - Uses OpenOCD or STM32 VS Code Extension
   - Interface: `stlink`
   - Target: `stm32f4x`

2. **J-Link Debug Configuration**
   - Uses Cortex-Debug extension with J-Link
   - Interface: `jlink`
   - Device: `STM32F401RE`

### Tasks

The project MUST provide `.vscode/tasks.json` tasks:

| Task | Description |
|------|-------------|
| `flash-stlink` | Flash firmware using ST-LINK |
| `flash-jlink` | Flash firmware using J-Link |
| `convert-to-jlink` | Run conversion script |
| `restore-stlink` | Run restoration script |

## Workflow Diagrams

### Development with J-Link OB (Preferred)

```
[Developer] --> [VS Code]
                   |
                   v
            [J-Link Debug Config]
                   |
                   v
            [JLinkGDBServer]
                   |
                   v
[USB] --> [J-Link OB on Nucleo] --> [STM32F401RE]
                   |
                   +-- RTT Channel 0: Command/Response
                   +-- RTT Channel 1: SystemView (reserved)
```

### Development with ST-LINK (Fallback)

```
[Developer] --> [VS Code]
                   |
                   v
            [ST-LINK Debug Config]
                   |
                   v
            [OpenOCD / ST-LINK Tools]
                   |
                   v
[USB] --> [ST-LINK on Nucleo] --> [STM32F401RE]
                   |
                   +-- VCP (USART2): Command/Response
```

## References

- [SEGGER ST-LINK On-Board](https://www.segger.com/products/debug-probes/j-link/models/other-j-links/st-link-on-board/) - Conversion utility and restore process
- [SEGGER J-Link Software](https://www.segger.com/downloads/jlink/) - J-Link tools download
- [ST UM1724](https://www.st.com/resource/en/user_manual/um1724-stm32-nucleo64-boards-mb1136-stmicroelectronics.pdf) - NUCLEO-F401RE User Manual
- [SEGGER STM32 Wiki](https://wiki.segger.com/STM32) - J-Link STM32 support

## Implementation Checklist

- [ ] Create `scripts/windows/convert_to_jlink.ps1`
- [ ] Create `scripts/windows/restore_stlink.ps1`
- [ ] Create `scripts/linux/convert_to_jlink.sh`
- [ ] Create `scripts/linux/restore_stlink.sh`
- [ ] Update `.vscode/launch.json` with dual debug configurations
- [ ] Update `.vscode/tasks.json` with flash and conversion tasks
- [ ] Test conversion and restoration on Windows
- [ ] Test conversion and restoration on Linux (Raspberry Pi)
