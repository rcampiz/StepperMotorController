/**
 * @file debug_commands.cpp
 * @brief Hardware debug/bringup command implementations
 *
 * All register-touching debug commands extracted from command_parser.cpp.
 * These are non-production diagnostic commands used during board bringup
 * (SPI bit-bang tests, GPIO probing, powerSTEP01 register dumps, encoder
 * TIM4 diagnostics).
 */

#include "wiring/debug_commands.hpp"
#include "X_vendor/CMSIS/stm32f401xe.h"
#include "F_platform/tasks/motor_task.hpp"
#include "F_platform/tasks/display_task.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Dispatch router
// ============================================================================

bool DebugCommandHandler::dispatch(const char* cmd, const char* const* args,
                                   uint8_t argCount, Comms::ITransport& transport) {
    if (strcmp(cmd, "SPI_DEBUG") == 0) { cmdSpiDebug(transport); return true; }
    if (strcmp(cmd, "SPI_MODE_TEST") == 0) { cmdSpiModeTest(transport); return true; }
    if (strcmp(cmd, "CS_LOW") == 0) { cmdCsLow(transport); return true; }
    if (strcmp(cmd, "CS_HIGH") == 0) { cmdCsHigh(transport); return true; }
    if (strcmp(cmd, "MISO_PULLDOWN") == 0) { cmdMisoPulldown(transport); return true; }
    if (strcmp(cmd, "MISO_NOPULL") == 0) { cmdMisoNopull(transport); return true; }
    if (strcmp(cmd, "STBY_RELEASE") == 0) { cmdStbyRelease(transport); return true; }
    if (strcmp(cmd, "STBY_HOLD") == 0) { cmdStbyHold(transport); return true; }
    if (strcmp(cmd, "GPIO_STATE") == 0) { cmdGpioState(transport); return true; }
    if (strcmp(cmd, "PA6_HIZ") == 0) { cmdPa6Hiz(transport); return true; }
    if (strcmp(cmd, "PA6_RESTORE") == 0) { cmdPa6Restore(transport); return true; }
    if (strcmp(cmd, "SPI_STOP") == 0) { cmdSpiStop(transport); return true; }
    if (strcmp(cmd, "SPI_START") == 0) { cmdSpiStart(transport); return true; }
    if (strcmp(cmd, "GPIO_DUMP") == 0) { cmdGpioDump(transport); return true; }
    if (strcmp(cmd, "SPI_FLOAT_TEST") == 0) { cmdSpiFloatTest(transport); return true; }
    if (strcmp(cmd, "SPI_BITBANG") == 0) { cmdSpiBitbang(transport); return true; }
    if (strcmp(cmd, "SPI_BITBANG_M3") == 0) { cmdSpiBitbangM3(transport); return true; }
    if (strcmp(cmd, "SPI_ECHO_TEST") == 0) { cmdSpiEchoTest(transport); return true; }
    if (strcmp(cmd, "SPI_CS_TOGGLE") == 0) { cmdSpiCsToggle(transport); return true; }
    if (strcmp(cmd, "PS01_DIAG") == 0) { cmdPs01Diag(transport); return true; }
    if (strcmp(cmd, "PS01_FIX_CLK") == 0) { cmdPs01FixClk(transport); return true; }
    if (strcmp(cmd, "PS01_SAFE") == 0) { cmdPs01Safe(transport); return true; }
    if (strcmp(cmd, "PS01_RESET") == 0) { cmdPs01Reset(transport); return true; }
    if (strcmp(cmd, "PS01_RUN") == 0) { cmdPs01Run(args, argCount, transport); return true; }
    if (strcmp(cmd, "PS01_STOP") == 0) { cmdPs01Stop(transport); return true; }
    if (strcmp(cmd, "LCD_DISABLE") == 0) { cmdLcdDisable(transport); return true; }
    if (strcmp(cmd, "MOSI_TEST") == 0) { cmdMosiTest(transport); return true; }
    if (strcmp(cmd, "SPI_LOOPBACK") == 0) { cmdSpiLoopback(transport); return true; }
    if (strcmp(cmd, "MOTOR_DEBUG") == 0) { cmdMotorDebug(transport); return true; }
    if (strcmp(cmd, "ENC_DEBUG") == 0) { cmdEncDebug(transport); return true; }
    return false;
}

// ============================================================================
// SPI2 diagnostic formatter (for FLASH_TEST)
// ============================================================================

int DebugCommandHandler::formatSpi2Diag(char* buf, size_t bufSize) {
    int pos = 0;
    // SPI2 peripheral registers
    pos += snprintf(buf + pos, bufSize - pos, " SPI2:CR1=%04lX,SR=%04lX",
                    (unsigned long)SPI2->CR1, (unsigned long)SPI2->SR);

    // SPI2 pin config: SCK = PB13
    auto sp = GPIOB;
    constexpr uint8_t sn = 13;
    uint32_t sckM = (sp->MODER >> (sn * 2)) & 0x3;
    uint32_t sckAF = (sp->AFR[sn / 8] >> ((sn % 8) * 4)) & 0xF;
    // MISO = PC2
    auto mp = GPIOC;
    constexpr uint8_t mn = 2;
    uint32_t misoM = (mp->MODER >> (mn * 2)) & 0x3;
    uint32_t misoAF = (mp->AFR[mn / 8] >> ((mn % 8) * 4)) & 0xF;
    uint32_t misoIDR = (mp->IDR >> mn) & 0x1;
    // MOSI = PC3
    auto op = GPIOC;
    constexpr uint8_t on = 3;
    uint32_t mosiM = (op->MODER >> (on * 2)) & 0x3;
    uint32_t mosiAF = (op->AFR[on / 8] >> ((on % 8) * 4)) & 0xF;
    pos += snprintf(buf + pos, bufSize - pos,
                    " SCK:M%lu/AF%lu MI:M%lu/AF%lu/IDR%lu MO:M%lu/AF%lu",
                    sckM, sckAF, misoM, misoAF, misoIDR, mosiM, mosiAF);
    return pos;
}

// ============================================================================
// SPI/GPIO debug commands
// ============================================================================

void DebugCommandHandler::cmdSpiDebug(Comms::ITransport& t) {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "SPI1_CR1=%08X SR=%04X MODER=%08X AFR0=%08X PC_ODR=%04X",
             (unsigned)SPI1->CR1, (unsigned)SPI1->SR, (unsigned)GPIOA->MODER,
             (unsigned)GPIOA->AFR[0], (unsigned)GPIOC->ODR);
    t.println(buf);
}

void DebugCommandHandler::cmdSpiModeTest(Comms::ITransport& t) {
    char buf[256];
    t.println("Testing SPI modes on motor CS (PB6/D10)...");

    for (int mode = 0; mode < 4; mode++) {
      SPI1->CR1 &= ~SPI_CR1_SPE;
      SPI1->CR1 &= ~(SPI_CR1_CPOL | SPI_CR1_CPHA);
      if (mode & 0x02)
        SPI1->CR1 |= SPI_CR1_CPOL;
      if (mode & 0x01)
        SPI1->CR1 |= SPI_CR1_CPHA;
      SPI1->CR1 |= SPI_CR1_SPE;

      GPIOB->BSRR = (1UL << (6 + 16)); // PB6 low
      for (volatile int i = 0; i < 100; i++)
        ;

      while (!(SPI1->SR & SPI_SR_TXE))
        ;
      *(volatile uint8_t *)&SPI1->DR = 0xD0;
      while (!(SPI1->SR & SPI_SR_RXNE))
        ;
      uint8_t dummy = *(volatile uint8_t *)&SPI1->DR;
      (void)dummy;

      while (!(SPI1->SR & SPI_SR_TXE))
        ;
      *(volatile uint8_t *)&SPI1->DR = 0x00;
      while (!(SPI1->SR & SPI_SR_RXNE))
        ;
      uint8_t hi = *(volatile uint8_t *)&SPI1->DR;

      while (!(SPI1->SR & SPI_SR_TXE))
        ;
      *(volatile uint8_t *)&SPI1->DR = 0x00;
      while (!(SPI1->SR & SPI_SR_RXNE))
        ;
      uint8_t lo = *(volatile uint8_t *)&SPI1->DR;

      while (SPI1->SR & SPI_SR_BSY)
        ;

      GPIOB->BSRR = (1UL << 6); // PB6 high

      uint16_t status = (hi << 8) | lo;
      snprintf(buf, sizeof(buf), "Mode%d: STATUS=%04X (hi=%02X lo=%02X)", mode,
               status, hi, lo);
      t.println(buf);
    }

    SPI1->CR1 &= ~SPI_CR1_SPE;
    SPI1->CR1 |= (SPI_CR1_CPOL | SPI_CR1_CPHA);
    SPI1->CR1 |= SPI_CR1_SPE;
    t.println("Done. Mode 3 restored.");
}

void DebugCommandHandler::cmdCsLow(Comms::ITransport& t) {
    GPIOB->BSRR = (1UL << (6 + 16));
    t.println("PB6 (CS/D10) forced LOW. Measure voltage now.");
    t.println("Send CS_HIGH to release.");
}

void DebugCommandHandler::cmdCsHigh(Comms::ITransport& t) {
    GPIOB->BSRR = (1UL << 6);
    t.println("PB6 (CS/D10) released HIGH.");
}

void DebugCommandHandler::cmdMisoPulldown(Comms::ITransport& t) {
    GPIOA->PUPDR &= ~(0x3UL << 12);
    GPIOA->PUPDR |= (0x2UL << 12);
    t.println("PA6 (MISO) pull-down ENABLED.");
}

void DebugCommandHandler::cmdMisoNopull(Comms::ITransport& t) {
    GPIOA->PUPDR &= ~(0x3UL << 12);
    t.println("PA6 (MISO) pull DISABLED.");
}

void DebugCommandHandler::cmdStbyRelease(Comms::ITransport& t) {
    GPIOA->MODER &= ~(0x3UL << 18);
    GPIOA->MODER |= (0x1UL << 18);
    GPIOA->BSRR = (1UL << 9);
    t.println("PA9 (STBY_RST) set HIGH - powerSTEP01 should be active.");
}

void DebugCommandHandler::cmdStbyHold(Comms::ITransport& t) {
    GPIOA->MODER &= ~(0x3UL << 18);
    GPIOA->MODER |= (0x1UL << 18);
    GPIOA->BSRR = (1UL << (9 + 16));
    t.println("PA9 (STBY_RST) set LOW - powerSTEP01 in standby/reset.");
}

void DebugCommandHandler::cmdGpioState(Comms::ITransport& t) {
    char buf[256];
    uint32_t pa_idr = GPIOA->IDR;
    uint32_t pb_idr = GPIOB->IDR;
    snprintf(buf, sizeof(buf),
             "PA: IDR=%04X (MISO/PA6=%d, STBY/PA9=%d, FLAG/PA10=%d)",
             (unsigned)(pa_idr & 0xFFFF), (int)((pa_idr >> 6) & 1),
             (int)((pa_idr >> 9) & 1), (int)((pa_idr >> 10) & 1));
    t.println(buf);
    snprintf(buf, sizeof(buf), "PB: IDR=%04X (BUSY/PB4=%d, CS/PB6=%d)",
             (unsigned)(pb_idr & 0xFFFF), (int)((pb_idr >> 4) & 1),
             (int)((pb_idr >> 6) & 1));
    t.println(buf);
}

void DebugCommandHandler::cmdPa6Hiz(Comms::ITransport& t) {
    char buf[128];
    GPIOA->MODER &= ~(0x3UL << 12);
    GPIOA->PUPDR &= ~(0x3UL << 12);
    snprintf(buf, sizeof(buf), "PA6 set to Hi-Z input. MODER=%08lX PUPDR=%08lX",
             (unsigned long)GPIOA->MODER, (unsigned long)GPIOA->PUPDR);
    t.println(buf);
    t.println("Measure PA6 to GND now - should be MΩ if no external load.");
    t.println("Send PA6_RESTORE to restore SPI config.");
}

void DebugCommandHandler::cmdPa6Restore(Comms::ITransport& t) {
    GPIOA->MODER &= ~(0x3UL << 12);
    GPIOA->MODER |= (0x2UL << 12);
    GPIOA->PUPDR &= ~(0x3UL << 12);
    GPIOA->PUPDR |= (0x2UL << 12);
    t.println("PA6 restored to SPI1 MISO with pull-down.");
}

void DebugCommandHandler::cmdSpiStop(Comms::ITransport& t) {
    char buf[80];
    Tasks::MotorTask_Suspend();
    t.println("Motor task suspended.");

    SPI1->CR1 &= ~SPI_CR1_SPE;
    GPIOA->MODER &=
        ~((0x3UL << 10) | (0x3UL << 12) | (0x3UL << 14));
    GPIOA->PUPDR &= ~((0x3UL << 10) | (0x3UL << 12) | (0x3UL << 14));
    GPIOB->MODER &= ~(0x3UL << 12);
    GPIOB->PUPDR &= ~(0x3UL << 12);

    t.println("SPI1 DISABLED. All pins Hi-Z.");
    snprintf(buf, sizeof(buf), "GPIOA MODER=%08lX GPIOB MODER=%08lX",
             (unsigned long)GPIOA->MODER, (unsigned long)GPIOB->MODER);
    t.println(buf);
    t.println("Measure pins. Send SPI_START to restore.");
}

void DebugCommandHandler::cmdSpiStart(Comms::ITransport& t) {
    char buf[128];
    GPIOA->MODER &= ~(0x3UL << 10);
    GPIOA->MODER |= (0x2UL << 10);
    GPIOA->MODER &= ~(0x3UL << 12);
    GPIOA->MODER |= (0x2UL << 12);
    GPIOA->PUPDR &= ~(0x3UL << 12);
    GPIOA->PUPDR |= (0x2UL << 12);
    GPIOA->MODER &= ~(0x3UL << 14);
    GPIOA->MODER |= (0x2UL << 14);

    GPIOB->MODER &= ~(0x3UL << 12);
    GPIOB->MODER |= (0x1UL << 12);
    GPIOB->BSRR = (1UL << 6);

    SPI1->CR1 |= SPI_CR1_SPE;

    t.println("SPI1 ENABLED. Pins restored.");
    snprintf(buf, sizeof(buf), "GPIOA MODER=%08lX SPI1 CR1=%04lX SR=%04lX",
             (unsigned long)GPIOA->MODER, (unsigned long)SPI1->CR1,
             (unsigned long)SPI1->SR);
    t.println(buf);

    Tasks::MotorTask_Resume();
    t.println("Motor task resumed.");
}

void DebugCommandHandler::cmdGpioDump(Comms::ITransport& t) {
    char buf[128];
    t.println("=== GPIOA Registers ===");
    snprintf(buf, sizeof(buf), "MODER=%08lX  (PA6 bits 13:12 = %lu)",
             (unsigned long)GPIOA->MODER,
             (unsigned long)((GPIOA->MODER >> 12) & 0x3));
    t.println(buf);
    snprintf(buf, sizeof(buf), "OTYPER=%04lX  OSPEEDR=%08lX",
             (unsigned long)GPIOA->OTYPER, (unsigned long)GPIOA->OSPEEDR);
    t.println(buf);
    snprintf(buf, sizeof(buf), "PUPDR=%08lX  (PA6 bits 13:12 = %lu)",
             (unsigned long)GPIOA->PUPDR,
             (unsigned long)((GPIOA->PUPDR >> 12) & 0x3));
    t.println(buf);
    snprintf(buf, sizeof(buf), "IDR=%04lX  ODR=%04lX",
             (unsigned long)GPIOA->IDR, (unsigned long)GPIOA->ODR);
    t.println(buf);
    snprintf(buf, sizeof(buf), "AFR[0]=%08lX  (PA6 bits 27:24 = %lu)",
             (unsigned long)GPIOA->AFR[0],
             (unsigned long)((GPIOA->AFR[0] >> 24) & 0xF));
    t.println(buf);
    t.println("=== SPI1 Registers ===");
    snprintf(buf, sizeof(buf), "CR1=%04lX  CR2=%04lX  SR=%04lX",
             (unsigned long)SPI1->CR1, (unsigned long)SPI1->CR2,
             (unsigned long)SPI1->SR);
    t.println(buf);
    t.println("PA6 MODER: 00=input, 01=output, 10=AF, 11=analog");
    t.println("PA6 PUPDR: 00=none, 01=up, 10=down");
}

void DebugCommandHandler::cmdSpiFloatTest(Comms::ITransport& t) {
    char buf[128];
    t.println("Testing SPI pins as floating inputs...");

    SPI1->CR1 &= ~SPI_CR1_SPE;
    uint32_t saved_moder = GPIOA->MODER;
    uint32_t saved_pupdr = GPIOA->PUPDR;

    GPIOA->MODER &=
        ~((0x3UL << 10) | (0x3UL << 12) | (0x3UL << 14));
    GPIOA->PUPDR &= ~((0x3UL << 10) | (0x3UL << 12) | (0x3UL << 14));

    for (volatile int d = 0; d < 1000; d++)
      ;

    uint32_t idr = GPIOA->IDR;
    int sck_state = (idr >> 5) & 1;
    int miso_state = (idr >> 6) & 1;
    int mosi_state = (idr >> 7) & 1;

    snprintf(buf, sizeof(buf), "Floating: SCK/PA5=%d, MISO/PA6=%d, MOSI/PA7=%d",
             sck_state, miso_state, mosi_state);
    t.println(buf);

    GPIOA->PUPDR |= ((0x1UL << 10) | (0x1UL << 12) | (0x1UL << 14));
    for (volatile int d = 0; d < 1000; d++)
      ;

    idr = GPIOA->IDR;
    sck_state = (idr >> 5) & 1;
    miso_state = (idr >> 6) & 1;
    mosi_state = (idr >> 7) & 1;

    snprintf(buf, sizeof(buf),
             "With pull-up: SCK/PA5=%d, MISO/PA6=%d, MOSI/PA7=%d", sck_state,
             miso_state, mosi_state);
    t.println(buf);

    t.println("If a pin stays 0 with pull-up, external pull-down exists.");
    t.println("If a pin floats (random), no external pull.");

    GPIOA->PUPDR = saved_pupdr;
    GPIOA->MODER = saved_moder;
    SPI1->CR1 |= SPI_CR1_SPE;
    t.println("SPI restored.");
}

// ============================================================================
// Bit-bang SPI tests
// ============================================================================

void DebugCommandHandler::cmdSpiBitbang(Comms::ITransport& t) {
    char buf[128];
    t.println("Bit-bang SPI test (bypasses SPI peripheral)...");

    SPI1->CR1 &= ~SPI_CR1_SPE;
    uint32_t saved_moder = GPIOA->MODER;

    GPIOA->MODER &= ~((0x3 << 10) | (0x3 << 12) | (0x3 << 14));
    GPIOA->MODER |= ((0x1 << 10) | (0x1 << 14));

    GPIOB->BSRR = (1UL << (6 + 16)); // PB6 low
    for (volatile int d = 0; d < 100; d++)
      ;

    uint8_t txByte = 0xD0;
    uint8_t rxByte = 0;

    for (int bit = 7; bit >= 0; bit--) {
      if (txByte & (1 << bit)) {
        GPIOA->BSRR = (1 << 7);
      } else {
        GPIOA->BSRR = (1 << (7 + 16));
      }
      for (volatile int d = 0; d < 10; d++)
        ;

      GPIOA->BSRR = (1 << 5);
      for (volatile int d = 0; d < 10; d++)
        ;

      if (GPIOA->IDR & (1 << 6)) {
        rxByte |= (1 << bit);
      }

      GPIOA->BSRR = (1 << (5 + 16));
      for (volatile int d = 0; d < 10; d++)
        ;
    }

    snprintf(buf, sizeof(buf), "TX: 0x%02X  RX: 0x%02X", txByte, rxByte);
    t.println(buf);

    uint8_t hi = 0, lo = 0;
    for (int byteNum = 0; byteNum < 2; byteNum++) {
      uint8_t rx = 0;
      for (int bit = 7; bit >= 0; bit--) {
        GPIOA->BSRR = (1 << (7 + 16));
        for (volatile int d = 0; d < 10; d++)
          ;

        GPIOA->BSRR = (1 << 5);
        for (volatile int d = 0; d < 10; d++)
          ;

        if (GPIOA->IDR & (1 << 6)) {
          rx |= (1 << bit);
        }

        GPIOA->BSRR = (1 << (5 + 16));
        for (volatile int d = 0; d < 10; d++)
          ;
      }
      if (byteNum == 0)
        hi = rx;
      else
        lo = rx;
    }

    GPIOB->BSRR = (1UL << 6); // PB6 high

    snprintf(buf, sizeof(buf), "STATUS: hi=0x%02X lo=0x%02X (raw=0x%04X)", hi,
             lo, (hi << 8) | lo);
    t.println(buf);

    for (volatile int d = 0; d < 100; d++)
      ;
    int miso_idle = (GPIOA->IDR >> 6) & 1;
    snprintf(buf, sizeof(buf), "MISO idle (CS high): %d", miso_idle);
    t.println(buf);

    GPIOA->MODER = saved_moder;
    SPI1->CR1 |= SPI_CR1_SPE;
    t.println("Done. SPI restored.");
}

void DebugCommandHandler::cmdSpiBitbangM3(Comms::ITransport& t) {
    char buf[128];
    t.println("Bit-bang SPI Mode 3 test (CPOL=1, CPHA=1)...");

    SPI1->CR1 &= ~SPI_CR1_SPE;
    uint32_t saved_moder = GPIOA->MODER;

    GPIOA->MODER &= ~((0x3UL << 10) | (0x3UL << 12) | (0x3UL << 14));
    GPIOA->MODER |=
        ((0x1UL << 10) | (0x1UL << 14));

    GPIOA->BSRR = (1UL << 5); // PA5 high (SCK idle)
    for (volatile int d = 0; d < 100; d++)
      ;

    GPIOB->BSRR = (1UL << (6 + 16)); // PB6 low
    for (volatile int d = 0; d < 100; d++)
      ;

    auto transferByteM3 = [](uint8_t tx) -> uint8_t {
      uint8_t rx = 0;
      for (int bit = 7; bit >= 0; bit--) {
        GPIOA->BSRR = (1UL << (5 + 16));
        if (tx & (1 << bit)) {
          GPIOA->BSRR = (1UL << 7);
        } else {
          GPIOA->BSRR = (1UL << (7 + 16));
        }
        for (volatile int d = 0; d < 20; d++)
          ;
        GPIOA->BSRR = (1UL << 5);
        for (volatile int d = 0; d < 20; d++)
          ;
        if (GPIOA->IDR & (1UL << 6)) {
          rx |= (1 << bit);
        }
      }
      return rx;
    };

    uint8_t rx0 = transferByteM3(0xD0);
    uint8_t rx1 = transferByteM3(0x00);
    uint8_t rx2 = transferByteM3(0x00);

    GPIOB->BSRR = (1UL << 6); // PB6 high

    snprintf(buf, sizeof(buf), "TX: 0xD0  RX0: 0x%02X", rx0);
    t.println(buf);
    snprintf(buf, sizeof(buf), "STATUS: hi=0x%02X lo=0x%02X (raw=0x%04X)", rx1,
             rx2, (rx1 << 8) | rx2);
    t.println(buf);

    for (volatile int d = 0; d < 100; d++)
      ;
    int miso_idle = (GPIOA->IDR >> 6) & 1;
    snprintf(buf, sizeof(buf), "MISO idle (CS high): %d", miso_idle);
    t.println(buf);

    uint16_t status = (rx1 << 8) | rx2;
    if (status != 0) {
      t.println("Status bits:");
      if (status & 0x0001)
        t.println("  HiZ: outputs in high-Z");
      if (!(status & 0x0002))
        t.println("  BUSY: command in progress");
      if (!(status & 0x0200))
        t.println("  UVLO: undervoltage!");
      if (!(status & 0x2000))
        t.println("  OCD: overcurrent!");
      if (!(status & 0x4000))
        t.println("  STEP_LOSS_A: stall bridge A!");
      if (!(status & 0x8000))
        t.println("  STEP_LOSS_B: stall bridge B!");
      if (status & 0x0080)
        t.println("  CMD_ERROR: bad command");
    }

    GPIOA->MODER = saved_moder;
    SPI1->CR1 |= SPI_CR1_SPE;
    t.println("Done. SPI restored.");
}

void DebugCommandHandler::cmdSpiEchoTest(Comms::ITransport& t) {
    char buf[128];
    t.println("SPI Echo/Coupling Test...");
    t.println("Sending various patterns to detect MOSI->MISO coupling");

    SPI1->CR1 &= ~SPI_CR1_SPE;
    uint32_t saved_moder = GPIOA->MODER;

    GPIOA->MODER &= ~((0x3UL << 10) | (0x3UL << 12) | (0x3UL << 14));
    GPIOA->MODER |= ((0x1UL << 10) | (0x1UL << 14));

    auto xfer = [](uint8_t tx) -> uint8_t {
      uint8_t rx = 0;
      GPIOA->BSRR = (1UL << 5);
      for (volatile int d = 0; d < 20; d++)
        ;
      for (int bit = 7; bit >= 0; bit--) {
        GPIOA->BSRR = (1UL << (5 + 16));
        if (tx & (1 << bit))
          GPIOA->BSRR = (1UL << 7);
        else
          GPIOA->BSRR = (1UL << (7 + 16));
        for (volatile int d = 0; d < 20; d++)
          ;
        GPIOA->BSRR = (1UL << 5);
        for (volatile int d = 0; d < 20; d++)
          ;
        if (GPIOA->IDR & (1UL << 6))
          rx |= (1 << bit);
      }
      return rx;
    };

    uint8_t patterns[] = {0xD0, 0x55, 0xAA, 0xFF, 0x00, 0x29};
    t.println("With CS LOW (PB6=0):");
    GPIOB->BSRR = (1UL << (6 + 16));
    for (volatile int d = 0; d < 100; d++)
      ;

    for (int p = 0; p < 6; p++) {
      uint8_t tx = patterns[p];
      uint8_t rx0 = xfer(tx);
      uint8_t rx1 = xfer(0x00);
      snprintf(buf, sizeof(buf),
               "  TX=0x%02X -> RX0=0x%02X, RX1(NOP)=0x%02X %s", tx, rx0, rx1,
               (rx1 == tx) ? "<- ECHO!" : "");
      t.println(buf);
    }
    GPIOB->BSRR = (1UL << 6);

    t.println("With CS HIGH (PB6=1) - chip should NOT respond:");
    for (volatile int d = 0; d < 100; d++)
      ;
    uint8_t rx_cs_hi_0 = xfer(0xD0);
    uint8_t rx_cs_hi_1 = xfer(0x00);
    snprintf(buf, sizeof(buf), "  TX=0xD0 -> RX=0x%02X, RX(NOP)=0x%02X",
             rx_cs_hi_0, rx_cs_hi_1);
    t.println(buf);

    GPIOA->MODER = saved_moder;
    SPI1->CR1 |= SPI_CR1_SPE;
    t.println("Done.");
}

void DebugCommandHandler::cmdSpiCsToggle(Comms::ITransport& t) {
    char buf[128];
    t.println("SPI with CS toggle per byte (powerSTEP01 requirement)...");
    t.println("CS must go HIGH for >=625ns between each byte!");

    SPI1->CR1 &= ~SPI_CR1_SPE;
    uint32_t saved_moder = GPIOA->MODER;

    GPIOA->MODER &= ~((0x3UL << 10) | (0x3UL << 12) | (0x3UL << 14));
    GPIOA->MODER |= ((0x1UL << 10) | (0x1UL << 14));

    auto xferByte = [](uint8_t tx) -> uint8_t {
      uint8_t rx = 0;
      GPIOA->BSRR = (1UL << 5);
      for (volatile int d = 0; d < 10; d++)
        ;
      for (int bit = 7; bit >= 0; bit--) {
        GPIOA->BSRR = (1UL << (5 + 16));
        if (tx & (1 << bit))
          GPIOA->BSRR = (1UL << 7);
        else
          GPIOA->BSRR = (1UL << (7 + 16));
        for (volatile int d = 0; d < 20; d++)
          ;
        GPIOA->BSRR = (1UL << 5);
        for (volatile int d = 0; d < 20; d++)
          ;
        if (GPIOA->IDR & (1UL << 6))
          rx |= (1 << bit);
      }
      return rx;
    };

    GPIOB->BSRR = (1UL << 6);
    GPIOA->BSRR = (1UL << 5);
    for (volatile int d = 0; d < 100; d++)
      ;

    t.println("GetStatus (0xD0) with CS toggle per byte:");

    GPIOB->BSRR = (1UL << (6 + 16));
    for (volatile int d = 0; d < 10; d++)
      ;
    uint8_t rx0 = xferByte(0xD0);
    GPIOB->BSRR = (1UL << 6);
    for (volatile int d = 0; d < 100; d++)
      ;

    GPIOB->BSRR = (1UL << (6 + 16));
    for (volatile int d = 0; d < 10; d++)
      ;
    uint8_t rx1 = xferByte(0x00);
    GPIOB->BSRR = (1UL << 6);
    for (volatile int d = 0; d < 100; d++)
      ;

    GPIOB->BSRR = (1UL << (6 + 16));
    for (volatile int d = 0; d < 10; d++)
      ;
    uint8_t rx2 = xferByte(0x00);
    GPIOB->BSRR = (1UL << 6);

    snprintf(buf, sizeof(buf), "  Cmd RX: 0x%02X", rx0);
    t.println(buf);
    snprintf(buf, sizeof(buf), "  STATUS: hi=0x%02X lo=0x%02X (raw=0x%04X)",
             rx1, rx2, (rx1 << 8) | rx2);
    t.println(buf);

    uint16_t status = (rx1 << 8) | rx2;
    t.println("  Status decode:");
    snprintf(buf, sizeof(buf), "    HiZ=%d BUSY=%d SW=%d DIR=%d",
             (status & 0x0001) ? 1 : 0, (status & 0x0002) ? 0 : 1,
             (status & 0x0004) ? 1 : 0, (status & 0x0010) ? 1 : 0);
    t.println(buf);
    snprintf(buf, sizeof(buf), "    UVLO=%d TH_SD=%d OCD=%d STALL=%d",
             (status & 0x0200) ? 0 : 1, (status & 0x0800) ? 0 : 1,
             (status & 0x8000) ? 0 : 1, ((status & 0x6000) != 0x6000) ? 1 : 0);
    t.println(buf);

    int miso_idle = (GPIOA->IDR >> 6) & 1;
    snprintf(buf, sizeof(buf), "  MISO idle: %d", miso_idle);
    t.println(buf);

    GPIOA->MODER = saved_moder;
    SPI1->CR1 |= SPI_CR1_SPE;
    t.println("Done.");
}

// ============================================================================
// powerSTEP01 bringup commands
// ============================================================================

// Shared helper: Mode 3 bit-bang transfer with CS toggle per byte
static uint8_t ps01XferByte(uint8_t tx) {
    uint8_t rx = 0;
    GPIOA->BSRR = (1UL << 5);
    GPIOB->BSRR = (1UL << (6 + 16));
    for (volatile int d = 0; d < 10; d++)
      ;
    for (int bit = 7; bit >= 0; bit--) {
      GPIOA->BSRR = (1UL << (5 + 16));
      if (tx & (1 << bit))
        GPIOA->BSRR = (1UL << 7);
      else
        GPIOA->BSRR = (1UL << (7 + 16));
      for (volatile int d = 0; d < 20; d++)
        ;
      GPIOA->BSRR = (1UL << 5);
      for (volatile int d = 0; d < 20; d++)
        ;
      if (GPIOA->IDR & (1UL << 6))
        rx |= (1 << bit);
    }
    GPIOB->BSRR = (1UL << 6);
    for (volatile int d = 0; d < 100; d++)
      ;
    return rx;
}

// Helper: enter bit-bang mode (disable SPI1, save/set GPIO)
static uint32_t ps01EnterBitBang() {
    SPI1->CR1 &= ~SPI_CR1_SPE;
    uint32_t saved_moder = GPIOA->MODER;
    GPIOA->MODER &= ~((0x3UL << 10) | (0x3UL << 12) | (0x3UL << 14));
    GPIOA->MODER |= ((0x1UL << 10) | (0x1UL << 14));
    return saved_moder;
}

// Helper: exit bit-bang mode (restore GPIO, re-enable SPI1)
static void ps01ExitBitBang(uint32_t saved_moder) {
    GPIOA->MODER = saved_moder;
    SPI1->CR1 |= SPI_CR1_SPE;
}

void DebugCommandHandler::cmdPs01Diag(Comms::ITransport& t) {
    char buf[128];
    t.println("powerSTEP01 Diagnostic Registers:");

    uint32_t saved = ps01EnterBitBang();

    auto readReg2 = [](uint8_t regAddr) -> uint16_t {
      ps01XferByte(0x20 | regAddr);
      uint8_t hi = ps01XferByte(0x00);
      uint8_t lo = ps01XferByte(0x00);
      return (hi << 8) | lo;
    };
    auto readReg1 = [](uint8_t regAddr) -> uint8_t {
      ps01XferByte(0x20 | regAddr);
      return ps01XferByte(0x00);
    };

    uint16_t config = readReg2(0x1A);
    uint8_t adc_out = readReg1(0x12);
    uint8_t alarm_en = readReg1(0x17);
    uint8_t ocd_th = readReg1(0x13);
    uint8_t stall_th = readReg1(0x14);
    uint16_t gatecfg1 = readReg2(0x18);
    uint8_t k_therm = readReg1(0x11);

    snprintf(buf, sizeof(buf), "  CONFIG: 0x%04X", config);
    t.println(buf);
    snprintf(buf, sizeof(buf), "  ADC_OUT: 0x%02X (raw ADC value)", adc_out);
    t.println(buf);
    snprintf(buf, sizeof(buf), "  ALARM_EN: 0x%02X", alarm_en);
    t.println(buf);
    snprintf(buf, sizeof(buf), "  OCD_TH: 0x%02X (overcurrent threshold)", ocd_th);
    t.println(buf);
    snprintf(buf, sizeof(buf), "  STALL_TH: 0x%02X (stall threshold)", stall_th);
    t.println(buf);
    snprintf(buf, sizeof(buf), "  GATECFG1: 0x%04X", gatecfg1);
    t.println(buf);
    snprintf(buf, sizeof(buf), "  K_THERM: 0x%02X (thermal compensation)", k_therm);
    t.println(buf);

    t.println("  CONFIG decode:");
    snprintf(buf, sizeof(buf),
             "    OSC_SEL=%d EXT_CLK=%d SW_MODE=%d EN_VSCOMP=%d", config & 0x7,
             (config >> 3) & 1, (config >> 4) & 1, (config >> 5) & 1);
    t.println(buf);
    snprintf(buf, sizeof(buf),
             "    OC_SD=%d POW_SR=%d F_PWM_DEC=%d F_PWM_INT=%d",
             (config >> 7) & 1, (config >> 8) & 3, (config >> 10) & 7,
             (config >> 13) & 7);
    t.println(buf);

    ps01ExitBitBang(saved);
    t.println("Done.");
}

void DebugCommandHandler::cmdPs01FixClk(Comms::ITransport& t) {
    char buf[128];
    t.println("Fixing powerSTEP01 clock config (internal 16MHz)...");

    uint32_t saved = ps01EnterBitBang();

    // Read current CONFIG
    ps01XferByte(0x20 | 0x1A);
    uint8_t hi = ps01XferByte(0x00);
    uint8_t lo = ps01XferByte(0x00);
    uint16_t oldConfig = (hi << 8) | lo;
    snprintf(buf, sizeof(buf), "  Old CONFIG: 0x%04X (EXT_CLK=%d)", oldConfig,
             (oldConfig >> 3) & 1);
    t.println(buf);

    uint16_t newConfig = oldConfig & ~(1 << 3);
    snprintf(buf, sizeof(buf), "  New CONFIG: 0x%04X (EXT_CLK=0)", newConfig);
    t.println(buf);

    ps01XferByte(0x00 | 0x1A);
    ps01XferByte((newConfig >> 8) & 0xFF);
    ps01XferByte(newConfig & 0xFF);

    // Verify
    ps01XferByte(0x20 | 0x1A);
    hi = ps01XferByte(0x00);
    lo = ps01XferByte(0x00);
    uint16_t verifyConfig = (hi << 8) | lo;
    snprintf(buf, sizeof(buf), "  Verify CONFIG: 0x%04X", verifyConfig);
    t.println(buf);

    // Read status to clear flags
    ps01XferByte(0xD0);
    hi = ps01XferByte(0x00);
    lo = ps01XferByte(0x00);
    snprintf(buf, sizeof(buf), "  STATUS after fix: 0x%04X", (hi << 8) | lo);
    t.println(buf);

    ps01ExitBitBang(saved);
    t.println("Done. Try MOVE command now.");
}

void DebugCommandHandler::cmdPs01Safe(Comms::ITransport& t) {
    char buf[128];
    t.println("Configuring safe settings (raise thresholds)...");

    uint32_t saved = ps01EnterBitBang();

    // 1. Lower KVAL values
    t.println("  KVAL_HOLD/RUN/ACC/DEC = 0x10 (low current)");
    ps01XferByte(0x00 | 0x09); ps01XferByte(0x10);
    ps01XferByte(0x00 | 0x0A); ps01XferByte(0x10);
    ps01XferByte(0x00 | 0x0B); ps01XferByte(0x10);
    ps01XferByte(0x00 | 0x0C); ps01XferByte(0x10);

    // 2. MAX OCD threshold
    t.println("  OCD_TH = 0x1F (max ~10A threshold)");
    ps01XferByte(0x00 | 0x13); ps01XferByte(0x1F);

    // 3. MAX STALL threshold
    t.println("  STALL_TH = 0x7F (max threshold)");
    ps01XferByte(0x00 | 0x14); ps01XferByte(0x7F);

    // 4. GATECFG2 max blanking
    t.println("  GATECFG2 = 0xFF (max blanking/deadtime)");
    ps01XferByte(0x00 | 0x19); ps01XferByte(0xFF);

    // 5. Disable OC_SD in CONFIG
    ps01XferByte(0x20 | 0x1A);
    uint8_t hi = ps01XferByte(0x00);
    uint8_t lo = ps01XferByte(0x00);
    uint16_t config = (hi << 8) | lo;
    config &= ~(1 << 7);
    snprintf(buf, sizeof(buf), "  CONFIG = 0x%04X (OC_SD disabled)", config);
    t.println(buf);
    ps01XferByte(0x00 | 0x1A);
    ps01XferByte((config >> 8) & 0xFF);
    ps01XferByte(config & 0xFF);

    // 6. Disable UVLO_ADC alarm
    t.println("  ALARM_EN = 0xEF (disable UVLO_ADC alarm)");
    ps01XferByte(0x00 | 0x17); ps01XferByte(0xEF);

    // GetStatus to clear flags
    ps01XferByte(0xD0);
    hi = ps01XferByte(0x00);
    lo = ps01XferByte(0x00);
    snprintf(buf, sizeof(buf), "  STATUS: 0x%04X", (hi << 8) | lo);
    t.println(buf);

    ps01ExitBitBang(saved);
    t.println("Done. Try MOVE now.");
}

void DebugCommandHandler::cmdPs01Reset(Comms::ITransport& t) {
    char buf[128];
    t.println("Resetting powerSTEP01 and clearing faults...");

    uint32_t saved = ps01EnterBitBang();

    // 1. HardHiZ
    t.println("  Sending HardHiZ (0xA8)...");
    ps01XferByte(0xA8);

    // 2. ResetDevice
    t.println("  Sending ResetDevice (0xC0)...");
    ps01XferByte(0xC0);
    for (volatile int d = 0; d < 50000; d++)
      ;

    // 3. GetStatus to clear latched faults
    t.println("  Clearing latched faults (GetStatus)...");
    ps01XferByte(0xD0);
    uint8_t hi = ps01XferByte(0x00);
    uint8_t lo = ps01XferByte(0x00);
    snprintf(buf, sizeof(buf), "  STATUS after reset: 0x%04X", (hi << 8) | lo);
    t.println(buf);

    // 4. Configure with thermal alarms disabled
    t.println("  ALARM_EN = 0xE8 (disable OCD, thermal, UVLO_ADC alarms)");
    ps01XferByte(0x00 | 0x17); ps01XferByte(0xE8);

    // 5. Safe KVAL
    t.println("  KVAL = 0x10 (low current)");
    ps01XferByte(0x00 | 0x09); ps01XferByte(0x10);
    ps01XferByte(0x00 | 0x0A); ps01XferByte(0x10);
    ps01XferByte(0x00 | 0x0B); ps01XferByte(0x10);
    ps01XferByte(0x00 | 0x0C); ps01XferByte(0x10);

    // 6. MAX OCD/STALL thresholds
    t.println("  OCD_TH = 0x1F, STALL_TH = 0x7F");
    ps01XferByte(0x00 | 0x13); ps01XferByte(0x1F);
    ps01XferByte(0x00 | 0x14); ps01XferByte(0x7F);

    // 7. GATECFG2 max blanking
    ps01XferByte(0x00 | 0x19); ps01XferByte(0xFF);

    // 8. Disable OC_SD in CONFIG
    ps01XferByte(0x20 | 0x1A);
    hi = ps01XferByte(0x00);
    lo = ps01XferByte(0x00);
    uint16_t config = (hi << 8) | lo;
    config &= ~(1 << 7);
    snprintf(buf, sizeof(buf), "  CONFIG = 0x%04X (OC_SD disabled)", config);
    t.println(buf);
    ps01XferByte(0x00 | 0x1A);
    ps01XferByte((config >> 8) & 0xFF);
    ps01XferByte(config & 0xFF);

    // 9. Final status
    ps01XferByte(0xD0);
    hi = ps01XferByte(0x00);
    lo = ps01XferByte(0x00);
    snprintf(buf, sizeof(buf), "  Final STATUS: 0x%04X", (hi << 8) | lo);
    t.println(buf);

    uint16_t status = (hi << 8) | lo;
    t.println("  Decoded:");
    snprintf(buf, sizeof(buf), "    HiZ=%d BUSY=%d DIR=%d MOT_STATUS=%d",
             (status >> 0) & 1, !((status >> 1) & 1), (status >> 4) & 1,
             (status >> 5) & 3);
    t.println(buf);
    snprintf(buf, sizeof(buf), "    UVLO=%d UVLO_ADC=%d TH_SD=%d TH_WRN=%d",
             !((status >> 9) & 1), !((status >> 10) & 1), !((status >> 11) & 1),
             !((status >> 12) & 1));
    t.println(buf);
    snprintf(buf, sizeof(buf), "    STALL_A=%d STALL_B=%d OCD=%d",
             !((status >> 13) & 1), !((status >> 14) & 1),
             !((status >> 15) & 1));
    t.println(buf);

    ps01ExitBitBang(saved);
    t.println("Reset complete. Try MOVE now.");
}

void DebugCommandHandler::cmdPs01Run(const char* const* args, uint8_t argCount,
                                     Comms::ITransport& t) {
    char buf[128];
    uint8_t kval = 0x40;
    uint32_t speed = 0x5000;
    bool fwd = true;

    if (argCount >= 1)
      kval = (uint8_t)strtoul(args[0], nullptr, 0);
    if (argCount >= 2)
      speed = (uint32_t)strtoul(args[1], nullptr, 0);
    if (argCount >= 3)
      fwd = strtol(args[2], nullptr, 0) != 0;

    snprintf(buf, sizeof(buf), "Running motor: KVAL=0x%02X speed=0x%lX dir=%s",
             kval, (unsigned long)speed, fwd ? "FWD" : "REV");
    t.println(buf);

    uint32_t saved = ps01EnterBitBang();

    // Set KVAL registers
    t.println("  Setting KVAL...");
    ps01XferByte(0x00 | 0x09); ps01XferByte(kval);
    ps01XferByte(0x00 | 0x0A); ps01XferByte(kval);
    ps01XferByte(0x00 | 0x0B); ps01XferByte(kval);
    ps01XferByte(0x00 | 0x0C); ps01XferByte(kval);

    // Set ACC/DEC
    t.println("  Setting ACC/DEC=0x08A...");
    ps01XferByte(0x00 | 0x05); ps01XferByte(0x00); ps01XferByte(0x8A);
    ps01XferByte(0x00 | 0x06); ps01XferByte(0x00); ps01XferByte(0x8A);

    // Set MAX_SPEED
    t.println("  Setting MAX_SPEED=0x41...");
    ps01XferByte(0x00 | 0x07); ps01XferByte(0x00); ps01XferByte(0x41);

    // Clear faults
    ps01XferByte(0xD0);
    uint8_t hi = ps01XferByte(0x00);
    uint8_t lo = ps01XferByte(0x00);
    snprintf(buf, sizeof(buf), "  STATUS before RUN: 0x%04X", (hi << 8) | lo);
    t.println(buf);

    // Send RUN command
    t.println("  Sending RUN command...");
    ps01XferByte(0x50 | (fwd ? 1 : 0));
    ps01XferByte((speed >> 16) & 0x0F);
    ps01XferByte((speed >> 8) & 0xFF);
    ps01XferByte(speed & 0xFF);

    // Check status
    for (volatile int d = 0; d < 10000; d++)
      ;
    ps01XferByte(0xD0);
    hi = ps01XferByte(0x00);
    lo = ps01XferByte(0x00);
    uint16_t status = (hi << 8) | lo;
    snprintf(buf, sizeof(buf), "  STATUS after RUN: 0x%04X", status);
    t.println(buf);

    snprintf(buf, sizeof(buf), "    HiZ=%d BUSY=%d MOT_STATUS=%d",
             (status >> 0) & 1, !((status >> 1) & 1), (status >> 5) & 3);
    t.println(buf);

    const char *motStr[] = {"STOPPED", "ACCELERATING", "DECELERATING",
                            "CONST_SPEED"};
    snprintf(buf, sizeof(buf), "    Motion: %s", motStr[(status >> 5) & 3]);
    t.println(buf);

    ps01ExitBitBang(saved);
    t.println("Use PS01_STOP to stop motor.");
}

void DebugCommandHandler::cmdPs01Stop(Comms::ITransport& t) {
    t.println("Stopping motor (SoftStop)...");

    uint32_t saved = ps01EnterBitBang();

    ps01XferByte(0xB0); // SoftStop

    for (volatile int d = 0; d < 10000; d++)
      ;
    ps01XferByte(0xD0);
    uint8_t hi = ps01XferByte(0x00);
    uint8_t lo = ps01XferByte(0x00);
    char buf[64];
    snprintf(buf, sizeof(buf), "  STATUS: 0x%04X", (hi << 8) | lo);
    t.println(buf);

    ps01ExitBitBang(saved);
    t.println("Done.");
}

// ============================================================================
// LCD/motor isolation and MOSI tests
// ============================================================================

void DebugCommandHandler::cmdLcdDisable(Comms::ITransport& t) {
    char buf[128];
    t.println("Disabling LCD and display task...");

    Tasks::DisplayTask_Suspend();
    t.println("Display task suspended.");

    // Float LCD pins
    GPIOC->MODER &= ~(0x3UL << 12); // PC6 input
    GPIOB->MODER &= ~(0x3UL << 20); // PB10 input

    // Float joystick pins
    GPIOC->MODER &= ~(0x3UL << 14); // PC7 input
    GPIOC->MODER &= ~(0x3UL << 18); // PC9 input
    GPIOC->MODER &= ~(0x3UL << 20); // PC10 input
    GPIOC->MODER &= ~(0x3UL << 24); // PC12 input

    // Ensure PC8 (motor CS) is configured as output HIGH
    GPIOC->MODER &= ~(0x3UL << 16);
    GPIOC->MODER |= (0x1UL << 16);
    GPIOC->BSRR = (1UL << 8);

    snprintf(buf, sizeof(buf), "GPIOC MODER=%08lX (PC8 bits[17:16]=%lu)",
             (unsigned long)GPIOC->MODER,
             (unsigned long)((GPIOC->MODER >> 16) & 0x3));
    t.println(buf);

    t.println("Reinitializing motor driver...");
    Tasks::MotorTask_Reinit();
    t.println("Motor driver reinitialized.");

    t.println("LCD disabled. SPI1 now exclusively for motor.");
    t.println("Motor commands (MOVE, RUN, etc.) should now work.");
}

void DebugCommandHandler::cmdMosiTest(Comms::ITransport& t) {
    char buf[128];
    t.println("MOSI (PA7) toggle test...");
    t.println("Connect scope to PA7 or jumper PA7->PA6 for loopback.");

    SPI1->CR1 &= ~SPI_CR1_SPE;
    uint32_t saved_moder = GPIOA->MODER;

    GPIOA->MODER &= ~((0x3UL << 12) | (0x3UL << 14));
    GPIOA->MODER |= (0x1UL << 14); // PA7 output

    // Toggle PA7 and read PA6
    for (int i = 0; i < 3; i++) {
      GPIOA->BSRR = (1UL << 7);
      for (volatile int d = 0; d < 1000; d++)
        ;
      int miso_hi = (GPIOA->IDR >> 6) & 1;

      GPIOA->BSRR = (1UL << (7 + 16));
      for (volatile int d = 0; d < 1000; d++)
        ;
      int miso_lo = (GPIOA->IDR >> 6) & 1;

      snprintf(buf, sizeof(buf), "  PA7=HIGH -> PA6=%d  PA7=LOW -> PA6=%d",
               miso_hi, miso_lo);
      t.println(buf);
    }

    // Verify PA7 ODR
    GPIOA->BSRR = (1UL << 7);
    int odr_hi = (GPIOA->ODR >> 7) & 1;
    GPIOA->BSRR = (1UL << (7 + 16));
    int odr_lo = (GPIOA->ODR >> 7) & 1;
    snprintf(buf, sizeof(buf), "  PA7 ODR verify: set=%d, clear=%d", odr_hi,
             odr_lo);
    t.println(buf);

    if (odr_hi != 1 || odr_lo != 0) {
      t.println("ERROR: PA7 ODR not responding to BSRR!");
      t.println("GPIO may be in wrong mode or locked.");
    } else {
      t.println("PA7 ODR toggles correctly.");
    }

    GPIOA->MODER = saved_moder;
    SPI1->CR1 |= SPI_CR1_SPE;
    t.println("Done. SPI restored.");
}

void DebugCommandHandler::cmdSpiLoopback(Comms::ITransport& t) {
    char buf[128];
    t.println("SPI LOOPBACK TEST");
    t.println("Requires: Jumper wire from PA7 (MOSI) to PA6 (MISO)");
    t.println("Disconnect powerSTEP01 MISO if possible, or ignore motor.");

    SPI1->CR1 &= ~SPI_CR1_SPE;
    uint32_t saved_moder = GPIOA->MODER;

    GPIOA->MODER &= ~((0x3UL << 10) | (0x3UL << 12) | (0x3UL << 14));
    GPIOA->MODER |= ((0x1UL << 10) | (0x1UL << 14));

    uint8_t testBytes[] = {0xAA, 0x55, 0xFF, 0x00, 0xD0};
    bool allPass = true;

    for (int ti = 0; ti < 5; ti++) {
      uint8_t tx = testBytes[ti];
      uint8_t rx = 0;

      GPIOA->BSRR = (1UL << 5);
      for (volatile int d = 0; d < 20; d++)
        ;

      for (int bit = 7; bit >= 0; bit--) {
        GPIOA->BSRR = (1UL << (5 + 16));
        if (tx & (1 << bit)) {
          GPIOA->BSRR = (1UL << 7);
        } else {
          GPIOA->BSRR = (1UL << (7 + 16));
        }
        for (volatile int d = 0; d < 20; d++)
          ;
        GPIOA->BSRR = (1UL << 5);
        for (volatile int d = 0; d < 20; d++)
          ;
        if (GPIOA->IDR & (1UL << 6)) {
          rx |= (1 << bit);
        }
      }

      bool pass = (tx == rx);
      if (!pass)
        allPass = false;
      snprintf(buf, sizeof(buf), "  TX=0x%02X  RX=0x%02X  %s", tx, rx,
               pass ? "PASS" : "FAIL");
      t.println(buf);
    }

    if (allPass) {
      t.println("LOOPBACK TEST PASSED! MOSI and MISO working.");
    } else {
      t.println("LOOPBACK TEST FAILED. Check jumper connection.");
    }

    GPIOA->MODER = saved_moder;
    SPI1->CR1 |= SPI_CR1_SPE;
    t.println("Done.");
}

// ============================================================================
// SCPI debug queries
// ============================================================================

void DebugCommandHandler::cmdMotorDebug(Comms::ITransport& t) {
    Tasks::MotorDebugInfo info;
    if (!Tasks::MotorTask_GetDebugInfo(info)) {
      t.println("ERROR Motor not initialized");
      return;
    }

    const char *hiZ = ((info.status & 0x0001) != 0) ? "HiZ" : "Active";
    const char *busy = ((info.status & 0x0002) != 0) ? "Idle" : "BUSY";
    bool cmdErr = (info.status & 0x0080) != 0;
    bool uvlo = (info.status & 0x0200) == 0;
    uint8_t thStatus = (info.status >> 11) & 0x3;
    bool ocd = (info.status & 0x2000) == 0;
    bool stepLossA = (info.status & 0x4000) == 0;
    bool stepLossB = (info.status & 0x8000) == 0;

    char buf[256];
    snprintf(buf, sizeof(buf),
             "STATUS=%04X (%s %s%s%s%s%s%s%s)\n"
             "KVAL: HOLD=%02X RUN=%02X ACC=%02X DEC=%02X\n"
             "ACC=%u DEC=%u MAXSPD=%u POS=%ld",
             (unsigned)info.status, hiZ, busy, cmdErr ? " CMDERR" : "",
             uvlo ? " UVLO" : "",
             thStatus >= 2 ? " TH_SD" : (thStatus >= 1 ? " TH_WARN" : ""),
             ocd ? " OCD" : "", stepLossA ? " STALL_A" : "",
             stepLossB ? " STALL_B" : "", (unsigned)info.kvalHold,
             (unsigned)info.kvalRun, (unsigned)info.kvalAcc,
             (unsigned)info.kvalDec, (unsigned)info.accel, (unsigned)info.decel,
             (unsigned)info.maxSpeed, (long)info.absPos);
    t.println(buf);

    snprintf(buf, sizeof(buf),
             "CHIP REGS: OCD_TH=%02X STALL_TH=%02X CONFIG=%04X ALARM_EN=%02X "
             "FS_SPD=%03X",
             (unsigned)info.ocdTh, (unsigned)info.stallTh, (unsigned)info.config,
             (unsigned)info.alarmEn, (unsigned)info.fsSpd);
    t.println(buf);

    // GPIO diagnostics for SPI1 motor path
    snprintf(buf, sizeof(buf),
             "CS(PC8): MODER=%X ODR=%X IDR=%X  "
             "RST(PA9): ODR=%X  "
             "SPI1: PA5_M=%X PA6_M=%X PA7_M=%X  "
             "FLAG=%X BUSY=%X",
             (unsigned)((GPIOC->MODER >> 16) & 0x3),
             (unsigned)((GPIOC->ODR >> 8) & 0x1),
             (unsigned)((GPIOC->IDR >> 8) & 0x1),
             (unsigned)((GPIOA->ODR >> 9) & 0x1),
             (unsigned)((GPIOA->MODER >> 10) & 0x3),
             (unsigned)((GPIOA->MODER >> 12) & 0x3),
             (unsigned)((GPIOA->MODER >> 14) & 0x3),
             (unsigned)((GPIOA->IDR >> 10) & 0x1),
             (unsigned)((GPIOB->IDR >> 5) & 0x1));
    t.println(buf);
}

void DebugCommandHandler::cmdEncDebug(Comms::ITransport& t) {
    char buf[256];
    snprintf(buf, sizeof(buf),
             "TIM4: CNT=%04X CR1=%04X SMCR=%04X CCMR1=%04X CCER=%04X ARR=%04X "
             "PB: MODER[12:15]=%X AFR0[24:31]=%02X IDR[6:7]=%X "
             "PC: ODR[2:3]=%X IDR[4]=%X",
             static_cast<unsigned>(TIM4->CNT & 0xFFFF),
             static_cast<unsigned>(TIM4->CR1), static_cast<unsigned>(TIM4->SMCR),
             static_cast<unsigned>(TIM4->CCMR1),
             static_cast<unsigned>(TIM4->CCER),
             static_cast<unsigned>(TIM4->ARR & 0xFFFF),
             static_cast<unsigned>((GPIOB->MODER >> 12) & 0xF),
             static_cast<unsigned>((GPIOB->AFR[0] >> 24) & 0xFF),
             static_cast<unsigned>((GPIOB->IDR >> 6) & 0x3),
             static_cast<unsigned>((GPIOC->ODR >> 2) & 0x3),
             static_cast<unsigned>((GPIOC->IDR >> 4) & 0x1));
    t.println(buf);
}
