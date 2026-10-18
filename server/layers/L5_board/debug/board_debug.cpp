/**
 * @file debug_commands.cpp
 * @brief Hardware debug/bringup command implementations
 *
 * All register-touching debug commands extracted from command_parser.cpp.
 * These are non-production diagnostic commands used during board bringup
 * (SPI bit-bang tests, GPIO probing, powerSTEP01 register dumps, encoder
 * TIM4 diagnostics).
 */

#include "L5_board/debug/board_debug.hpp"
#include "harness/defs/harness_l4_l5.hpp"
#include "X_vendor/CMSIS/stm32f401xe.h"
#include "harness/pins/imicrosecond_clock.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace Board {

// ============================================================================
// Dispatch router
// ============================================================================

bool BoardDebug::rawCommand(const char* cmd, const char* const* args,
                                   uint8_t argCount, Harness::IResponseSink& transport) {
    if (strcmp(cmd, "SPI_DEBUG") == 0) { cmdSpiDebug(transport); return true; }
    if (strcmp(cmd, "SPI_MODE_TEST") == 0) { cmdSpiModeTest(transport); return true; }
    if (strcmp(cmd, "CS_LOW") == 0) { cmdCsLow(transport); return true; }
    if (strcmp(cmd, "CS_HIGH") == 0) { cmdCsHigh(transport); return true; }
    if (strcmp(cmd, "MISO_PULLDOWN") == 0) { cmdMisoPulldown(transport); return true; }
    if (strcmp(cmd, "MISO_NOPULL") == 0) { cmdMisoNopull(transport); return true; }
    if (strcmp(cmd, "STBY_RELEASE") == 0) { cmdStbyRelease(transport); return true; }
    if (strcmp(cmd, "STBY_HOLD") == 0) { cmdStbyHold(transport); return true; }
    if (strcmp(cmd, "STBY_SENSE") == 0) { cmdStbySense(transport); return true; }
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
    if (strcmp(cmd, "ENC_DEBUG") == 0) { cmdEncDebug(transport); return true; }
    return false;
}

// ============================================================================
// SPI2 diagnostic formatter (for FLASH_TEST)
// ============================================================================

int BoardDebug::formatSpi2Diag(char* buf, size_t bufSize) {
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

void BoardDebug::cmdSpiDebug(Harness::IResponseSink& t) {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "SPI1_CR1=%08X SR=%04X MODER=%08X AFR0=%08X PC_ODR=%04X",
             (unsigned)SPI1->CR1, (unsigned)SPI1->SR, (unsigned)GPIOA->MODER,
             (unsigned)GPIOA->AFR[0], (unsigned)GPIOC->ODR);
    t.writeLine(buf);
}

void BoardDebug::cmdSpiModeTest(Harness::IResponseSink& t) {
    char buf[256];
    t.writeLine("Testing SPI modes on motor CS (PB6/D10)...");

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
      t.writeLine(buf);
    }

    SPI1->CR1 &= ~SPI_CR1_SPE;
    SPI1->CR1 |= (SPI_CR1_CPOL | SPI_CR1_CPHA);
    SPI1->CR1 |= SPI_CR1_SPE;
    t.writeLine("Done. Mode 3 restored.");
}

void BoardDebug::cmdCsLow(Harness::IResponseSink& t) {
    GPIOB->BSRR = (1UL << (6 + 16));
    t.writeLine("PB6 (CS/D10) forced LOW. Measure voltage now.");
    t.writeLine("Send CS_HIGH to release.");
}

void BoardDebug::cmdCsHigh(Harness::IResponseSink& t) {
    GPIOB->BSRR = (1UL << 6);
    t.writeLine("PB6 (CS/D10) released HIGH.");
}

void BoardDebug::cmdMisoPulldown(Harness::IResponseSink& t) {
    GPIOA->PUPDR &= ~(0x3UL << 12);
    GPIOA->PUPDR |= (0x2UL << 12);
    t.writeLine("PA6 (MISO) pull-down ENABLED.");
}

void BoardDebug::cmdMisoNopull(Harness::IResponseSink& t) {
    GPIOA->PUPDR &= ~(0x3UL << 12);
    t.writeLine("PA6 (MISO) pull DISABLED.");
}

void BoardDebug::cmdStbyRelease(Harness::IResponseSink& t) {
    GPIOA->MODER &= ~(0x3UL << 18);
    GPIOA->MODER |= (0x1UL << 18);
    GPIOA->BSRR = (1UL << 9);
    t.writeLine("PA9 (STBY_RST) set HIGH - powerSTEP01 should be active.");
}

void BoardDebug::cmdStbyHold(Harness::IResponseSink& t) {
    GPIOA->MODER &= ~(0x3UL << 18);
    GPIOA->MODER |= (0x1UL << 18);
    GPIOA->BSRR = (1UL << (9 + 16));
    t.writeLine("PA9 (STBY_RST) set LOW - powerSTEP01 in standby/reset.");
}

void BoardDebug::cmdStbySense(Harness::IResponseSink& t) {
    // Let PA9 float (input, no pull) and watch what the shield does with STBY/RESET when
    // the MCU lets go. UM1910 figure 3: the RESET_IC net has no resistor on the shield, only
    // C6 = 100 nF to ground, so the node decays through the powerSTEP01's internal 450 k
    // pull-down with tau ~ 45 ms. A node that stays high for >1 s would mean a pull-up
    // (not fail-safe); a fall within ~100 ms is the capacitor discharging (fail-safe, slow).
    // Floating low puts the chip in standby and resets its registers: DRV:REINIT after.
    Platform::IMicrosecondClock* us = Harness::microsecondClock();
    const uint32_t moder = GPIOA->MODER;
    const uint32_t pupdr = GPIOA->PUPDR;
    GPIOA->PUPDR &= ~(0x3UL << 18);
    GPIOA->MODER &= ~(0x3UL << 18);
    const uint32_t t0 = (us != nullptr) ? us->getTickUs() : 0;
    int level = 1;
    uint32_t fellAtUs = 0;
    bool fell = false;
    while (!fell) {
        const uint32_t now = (us != nullptr) ? us->getTickUs() : t0 + 1000001U;
        level = static_cast<int>((GPIOA->IDR >> 9) & 1U);
        if (level == 0) { fell = true; fellAtUs = now - t0; }
        if (now - t0 > 1000000U) break;                  // give up after 1 s
    }
    GPIOA->MODER = moder;
    GPIOA->PUPDR = pupdr;
    GPIOA->BSRR = (1UL << 9);                            // release STBY again (driven high)
    char buf[160];
    if (fell) {
        snprintf(buf, sizeof(buf),
                 "STBY_SENSE PA9 floating fell LOW after %lu us: no pull-up on the shield, the "
                 "node discharges through the chip's 450k pull-down (fail-safe, slow)",
                 static_cast<unsigned long>(fellAtUs));
    } else {
        snprintf(buf, sizeof(buf),
                 "STBY_SENSE PA9 floating stayed HIGH for 1 s: a pull-up holds STBY/RESET "
                 "(NOT fail-safe when the MCU dies)");
    }
    t.writeLine(buf);
    t.writeLine("powerSTEP01 registers may have reset: send DRV:REINIT.");
}

void BoardDebug::cmdGpioState(Harness::IResponseSink& t) {
    char buf[256];
    uint32_t pa_idr = GPIOA->IDR;
    uint32_t pb_idr = GPIOB->IDR;
    snprintf(buf, sizeof(buf),
             "PA: IDR=%04X (MISO/PA6=%d, STBY/PA9=%d, FLAG/PA10=%d)",
             (unsigned)(pa_idr & 0xFFFF), (int)((pa_idr >> 6) & 1),
             (int)((pa_idr >> 9) & 1), (int)((pa_idr >> 10) & 1));
    t.writeLine(buf);
    snprintf(buf, sizeof(buf), "PB: IDR=%04X (BUSY/PB5=%d, ENC_A/PB6=%d, ENC_B/PB7=%d)",
             (unsigned)(pb_idr & 0xFFFF), (int)((pb_idr >> 5) & 1),
             (int)((pb_idr >> 6) & 1), (int)((pb_idr >> 7) & 1));
    t.writeLine(buf);
}

void BoardDebug::cmdPa6Hiz(Harness::IResponseSink& t) {
    char buf[128];
    GPIOA->MODER &= ~(0x3UL << 12);
    GPIOA->PUPDR &= ~(0x3UL << 12);
    snprintf(buf, sizeof(buf), "PA6 set to Hi-Z input. MODER=%08lX PUPDR=%08lX",
             (unsigned long)GPIOA->MODER, (unsigned long)GPIOA->PUPDR);
    t.writeLine(buf);
    t.writeLine("Measure PA6 to GND now - should be MΩ if no external load.");
    t.writeLine("Send PA6_RESTORE to restore SPI config.");
}

void BoardDebug::cmdPa6Restore(Harness::IResponseSink& t) {
    GPIOA->MODER &= ~(0x3UL << 12);
    GPIOA->MODER |= (0x2UL << 12);
    GPIOA->PUPDR &= ~(0x3UL << 12);
    GPIOA->PUPDR |= (0x2UL << 12);
    t.writeLine("PA6 restored to SPI1 MISO with pull-down.");
}

void BoardDebug::cmdSpiStop(Harness::IResponseSink& t) {
    char buf[80];

    SPI1->CR1 &= ~SPI_CR1_SPE;
    GPIOA->MODER &=
        ~((0x3UL << 10) | (0x3UL << 12) | (0x3UL << 14));
    GPIOA->PUPDR &= ~((0x3UL << 10) | (0x3UL << 12) | (0x3UL << 14));
    GPIOB->MODER &= ~(0x3UL << 12);
    GPIOB->PUPDR &= ~(0x3UL << 12);

    t.writeLine("SPI1 DISABLED. All pins Hi-Z.");
    snprintf(buf, sizeof(buf), "GPIOA MODER=%08lX GPIOB MODER=%08lX",
             (unsigned long)GPIOA->MODER, (unsigned long)GPIOB->MODER);
    t.writeLine(buf);
    t.writeLine("Measure pins. Send SPI_START to restore.");
}

void BoardDebug::cmdSpiStart(Harness::IResponseSink& t) {
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

    t.writeLine("SPI1 ENABLED. Pins restored.");
    snprintf(buf, sizeof(buf), "GPIOA MODER=%08lX SPI1 CR1=%04lX SR=%04lX",
             (unsigned long)GPIOA->MODER, (unsigned long)SPI1->CR1,
             (unsigned long)SPI1->SR);
    t.writeLine(buf);

}

void BoardDebug::cmdGpioDump(Harness::IResponseSink& t) {
    char buf[128];
    t.writeLine("=== GPIOA Registers ===");
    snprintf(buf, sizeof(buf), "MODER=%08lX  (PA6 bits 13:12 = %lu)",
             (unsigned long)GPIOA->MODER,
             (unsigned long)((GPIOA->MODER >> 12) & 0x3));
    t.writeLine(buf);
    snprintf(buf, sizeof(buf), "OTYPER=%04lX  OSPEEDR=%08lX",
             (unsigned long)GPIOA->OTYPER, (unsigned long)GPIOA->OSPEEDR);
    t.writeLine(buf);
    snprintf(buf, sizeof(buf), "PUPDR=%08lX  (PA6 bits 13:12 = %lu)",
             (unsigned long)GPIOA->PUPDR,
             (unsigned long)((GPIOA->PUPDR >> 12) & 0x3));
    t.writeLine(buf);
    snprintf(buf, sizeof(buf), "IDR=%04lX  ODR=%04lX",
             (unsigned long)GPIOA->IDR, (unsigned long)GPIOA->ODR);
    t.writeLine(buf);
    snprintf(buf, sizeof(buf), "AFR[0]=%08lX  (PA6 bits 27:24 = %lu)",
             (unsigned long)GPIOA->AFR[0],
             (unsigned long)((GPIOA->AFR[0] >> 24) & 0xF));
    t.writeLine(buf);
    t.writeLine("=== SPI1 Registers ===");
    snprintf(buf, sizeof(buf), "CR1=%04lX  CR2=%04lX  SR=%04lX",
             (unsigned long)SPI1->CR1, (unsigned long)SPI1->CR2,
             (unsigned long)SPI1->SR);
    t.writeLine(buf);
    t.writeLine("PA6 MODER: 00=input, 01=output, 10=AF, 11=analog");
    t.writeLine("PA6 PUPDR: 00=none, 01=up, 10=down");
}

void BoardDebug::cmdSpiFloatTest(Harness::IResponseSink& t) {
    char buf[128];
    t.writeLine("Testing SPI pins as floating inputs...");

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
    t.writeLine(buf);

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
    t.writeLine(buf);

    t.writeLine("If a pin stays 0 with pull-up, external pull-down exists.");
    t.writeLine("If a pin floats (random), no external pull.");

    GPIOA->PUPDR = saved_pupdr;
    GPIOA->MODER = saved_moder;
    SPI1->CR1 |= SPI_CR1_SPE;
    t.writeLine("SPI restored.");
}

// ============================================================================
// Bit-bang SPI tests
// ============================================================================

void BoardDebug::cmdSpiBitbang(Harness::IResponseSink& t) {
    char buf[128];
    t.writeLine("Bit-bang SPI test (bypasses SPI peripheral)...");

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
    t.writeLine(buf);

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
    t.writeLine(buf);

    for (volatile int d = 0; d < 100; d++)
      ;
    int miso_idle = (GPIOA->IDR >> 6) & 1;
    snprintf(buf, sizeof(buf), "MISO idle (CS high): %d", miso_idle);
    t.writeLine(buf);

    GPIOA->MODER = saved_moder;
    SPI1->CR1 |= SPI_CR1_SPE;
    t.writeLine("Done. SPI restored.");
}

void BoardDebug::cmdSpiBitbangM3(Harness::IResponseSink& t) {
    char buf[128];
    t.writeLine("Bit-bang SPI Mode 3 test (CPOL=1, CPHA=1)...");

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
    t.writeLine(buf);
    snprintf(buf, sizeof(buf), "STATUS: hi=0x%02X lo=0x%02X (raw=0x%04X)", rx1,
             rx2, (rx1 << 8) | rx2);
    t.writeLine(buf);

    for (volatile int d = 0; d < 100; d++)
      ;
    int miso_idle = (GPIOA->IDR >> 6) & 1;
    snprintf(buf, sizeof(buf), "MISO idle (CS high): %d", miso_idle);
    t.writeLine(buf);

    uint16_t status = (rx1 << 8) | rx2;
    if (status != 0) {
      t.writeLine("Status bits:");
      if (status & 0x0001)
        t.writeLine("  HiZ: outputs in high-Z");
      if (!(status & 0x0002))
        t.writeLine("  BUSY: command in progress");
      if (!(status & 0x0200))
        t.writeLine("  UVLO: undervoltage!");
      if (!(status & 0x2000))
        t.writeLine("  OCD: overcurrent!");
      if (!(status & 0x4000))
        t.writeLine("  STEP_LOSS_A: stall bridge A!");
      if (!(status & 0x8000))
        t.writeLine("  STEP_LOSS_B: stall bridge B!");
      if (status & 0x0080)
        t.writeLine("  CMD_ERROR: bad command");
    }

    GPIOA->MODER = saved_moder;
    SPI1->CR1 |= SPI_CR1_SPE;
    t.writeLine("Done. SPI restored.");
}

void BoardDebug::cmdSpiEchoTest(Harness::IResponseSink& t) {
    char buf[128];
    t.writeLine("SPI Echo/Coupling Test...");
    t.writeLine("Sending various patterns to detect MOSI->MISO coupling");

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
    t.writeLine("With CS LOW (PB6=0):");
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
      t.writeLine(buf);
    }
    GPIOB->BSRR = (1UL << 6);

    t.writeLine("With CS HIGH (PB6=1) - chip should NOT respond:");
    for (volatile int d = 0; d < 100; d++)
      ;
    uint8_t rx_cs_hi_0 = xfer(0xD0);
    uint8_t rx_cs_hi_1 = xfer(0x00);
    snprintf(buf, sizeof(buf), "  TX=0xD0 -> RX=0x%02X, RX(NOP)=0x%02X",
             rx_cs_hi_0, rx_cs_hi_1);
    t.writeLine(buf);

    GPIOA->MODER = saved_moder;
    SPI1->CR1 |= SPI_CR1_SPE;
    t.writeLine("Done.");
}

void BoardDebug::cmdSpiCsToggle(Harness::IResponseSink& t) {
    char buf[128];
    t.writeLine("SPI with CS toggle per byte (powerSTEP01 requirement)...");
    t.writeLine("CS must go HIGH for >=625ns between each byte!");

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

    t.writeLine("GetStatus (0xD0) with CS toggle per byte:");

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
    t.writeLine(buf);
    snprintf(buf, sizeof(buf), "  STATUS: hi=0x%02X lo=0x%02X (raw=0x%04X)",
             rx1, rx2, (rx1 << 8) | rx2);
    t.writeLine(buf);

    uint16_t status = (rx1 << 8) | rx2;
    t.writeLine("  Status decode:");
    snprintf(buf, sizeof(buf), "    HiZ=%d BUSY=%d SW=%d DIR=%d",
             (status & 0x0001) ? 1 : 0, (status & 0x0002) ? 0 : 1,
             (status & 0x0004) ? 1 : 0, (status & 0x0010) ? 1 : 0);
    t.writeLine(buf);
    snprintf(buf, sizeof(buf), "    UVLO=%d TH_SD=%d OCD=%d STALL=%d",
             (status & 0x0200) ? 0 : 1, (status & 0x0800) ? 0 : 1,
             (status & 0x8000) ? 0 : 1, ((status & 0x6000) != 0x6000) ? 1 : 0);
    t.writeLine(buf);

    int miso_idle = (GPIOA->IDR >> 6) & 1;
    snprintf(buf, sizeof(buf), "  MISO idle: %d", miso_idle);
    t.writeLine(buf);

    GPIOA->MODER = saved_moder;
    SPI1->CR1 |= SPI_CR1_SPE;
    t.writeLine("Done.");
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

void BoardDebug::cmdPs01Diag(Harness::IResponseSink& t) {
    char buf[128];
    t.writeLine("powerSTEP01 Diagnostic Registers:");

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
    t.writeLine(buf);
    snprintf(buf, sizeof(buf), "  ADC_OUT: 0x%02X (raw ADC value)", adc_out);
    t.writeLine(buf);
    snprintf(buf, sizeof(buf), "  ALARM_EN: 0x%02X", alarm_en);
    t.writeLine(buf);
    snprintf(buf, sizeof(buf), "  OCD_TH: 0x%02X (overcurrent threshold)", ocd_th);
    t.writeLine(buf);
    snprintf(buf, sizeof(buf), "  STALL_TH: 0x%02X (stall threshold)", stall_th);
    t.writeLine(buf);
    snprintf(buf, sizeof(buf), "  GATECFG1: 0x%04X", gatecfg1);
    t.writeLine(buf);
    snprintf(buf, sizeof(buf), "  K_THERM: 0x%02X (thermal compensation)", k_therm);
    t.writeLine(buf);

    t.writeLine("  CONFIG decode:");
    snprintf(buf, sizeof(buf),
             "    OSC_SEL=%d EXT_CLK=%d SW_MODE=%d EN_VSCOMP=%d", config & 0x7,
             (config >> 3) & 1, (config >> 4) & 1, (config >> 5) & 1);
    t.writeLine(buf);
    snprintf(buf, sizeof(buf),
             "    OC_SD=%d POW_SR=%d F_PWM_DEC=%d F_PWM_INT=%d",
             (config >> 7) & 1, (config >> 8) & 3, (config >> 10) & 7,
             (config >> 13) & 7);
    t.writeLine(buf);

    ps01ExitBitBang(saved);
    t.writeLine("Done.");
}

void BoardDebug::cmdPs01FixClk(Harness::IResponseSink& t) {
    char buf[128];
    t.writeLine("Fixing powerSTEP01 clock config (internal 16MHz)...");

    uint32_t saved = ps01EnterBitBang();

    // Read current CONFIG
    ps01XferByte(0x20 | 0x1A);
    uint8_t hi = ps01XferByte(0x00);
    uint8_t lo = ps01XferByte(0x00);
    uint16_t oldConfig = (hi << 8) | lo;
    snprintf(buf, sizeof(buf), "  Old CONFIG: 0x%04X (EXT_CLK=%d)", oldConfig,
             (oldConfig >> 3) & 1);
    t.writeLine(buf);

    uint16_t newConfig = oldConfig & ~(1 << 3);
    snprintf(buf, sizeof(buf), "  New CONFIG: 0x%04X (EXT_CLK=0)", newConfig);
    t.writeLine(buf);

    ps01XferByte(0x00 | 0x1A);
    ps01XferByte((newConfig >> 8) & 0xFF);
    ps01XferByte(newConfig & 0xFF);

    // Verify
    ps01XferByte(0x20 | 0x1A);
    hi = ps01XferByte(0x00);
    lo = ps01XferByte(0x00);
    uint16_t verifyConfig = (hi << 8) | lo;
    snprintf(buf, sizeof(buf), "  Verify CONFIG: 0x%04X", verifyConfig);
    t.writeLine(buf);

    // Read status to clear flags
    ps01XferByte(0xD0);
    hi = ps01XferByte(0x00);
    lo = ps01XferByte(0x00);
    snprintf(buf, sizeof(buf), "  STATUS after fix: 0x%04X", (hi << 8) | lo);
    t.writeLine(buf);

    ps01ExitBitBang(saved);
    t.writeLine("Done. Try MOVE command now.");
}

void BoardDebug::cmdPs01Safe(Harness::IResponseSink& t) {
    char buf[128];
    t.writeLine("Configuring safe settings (raise thresholds)...");

    uint32_t saved = ps01EnterBitBang();

    // 1. Lower KVAL values
    t.writeLine("  KVAL_HOLD/RUN/ACC/DEC = 0x10 (low current)");
    ps01XferByte(0x00 | 0x09); ps01XferByte(0x10);
    ps01XferByte(0x00 | 0x0A); ps01XferByte(0x10);
    ps01XferByte(0x00 | 0x0B); ps01XferByte(0x10);
    ps01XferByte(0x00 | 0x0C); ps01XferByte(0x10);

    // 2. MAX OCD threshold
    t.writeLine("  OCD_TH = 0x1F (max ~10A threshold)");
    ps01XferByte(0x00 | 0x13); ps01XferByte(0x1F);

    // 3. MAX STALL threshold
    t.writeLine("  STALL_TH = 0x7F (max threshold)");
    ps01XferByte(0x00 | 0x14); ps01XferByte(0x7F);

    // 4. GATECFG2 max blanking
    t.writeLine("  GATECFG2 = 0xFF (max blanking/deadtime)");
    ps01XferByte(0x00 | 0x19); ps01XferByte(0xFF);

    // 5. Disable OC_SD in CONFIG
    ps01XferByte(0x20 | 0x1A);
    uint8_t hi = ps01XferByte(0x00);
    uint8_t lo = ps01XferByte(0x00);
    uint16_t config = (hi << 8) | lo;
    config &= ~(1 << 7);
    snprintf(buf, sizeof(buf), "  CONFIG = 0x%04X (OC_SD disabled)", config);
    t.writeLine(buf);
    ps01XferByte(0x00 | 0x1A);
    ps01XferByte((config >> 8) & 0xFF);
    ps01XferByte(config & 0xFF);

    // 6. Disable UVLO_ADC alarm
    t.writeLine("  ALARM_EN = 0xEF (disable UVLO_ADC alarm)");
    ps01XferByte(0x00 | 0x17); ps01XferByte(0xEF);

    // GetStatus to clear flags
    ps01XferByte(0xD0);
    hi = ps01XferByte(0x00);
    lo = ps01XferByte(0x00);
    snprintf(buf, sizeof(buf), "  STATUS: 0x%04X", (hi << 8) | lo);
    t.writeLine(buf);

    ps01ExitBitBang(saved);
    t.writeLine("Done. Try MOVE now.");
}

void BoardDebug::cmdPs01Reset(Harness::IResponseSink& t) {
    char buf[128];
    t.writeLine("Resetting powerSTEP01 and clearing faults...");

    uint32_t saved = ps01EnterBitBang();

    // 1. HardHiZ
    t.writeLine("  Sending HardHiZ (0xA8)...");
    ps01XferByte(0xA8);

    // 2. ResetDevice
    t.writeLine("  Sending ResetDevice (0xC0)...");
    ps01XferByte(0xC0);
    for (volatile int d = 0; d < 50000; d++)
      ;

    // 3. GetStatus to clear latched faults
    t.writeLine("  Clearing latched faults (GetStatus)...");
    ps01XferByte(0xD0);
    uint8_t hi = ps01XferByte(0x00);
    uint8_t lo = ps01XferByte(0x00);
    snprintf(buf, sizeof(buf), "  STATUS after reset: 0x%04X", (hi << 8) | lo);
    t.writeLine(buf);

    // 4. Configure with thermal alarms disabled
    t.writeLine("  ALARM_EN = 0xE8 (disable OCD, thermal, UVLO_ADC alarms)");
    ps01XferByte(0x00 | 0x17); ps01XferByte(0xE8);

    // 5. Safe KVAL
    t.writeLine("  KVAL = 0x10 (low current)");
    ps01XferByte(0x00 | 0x09); ps01XferByte(0x10);
    ps01XferByte(0x00 | 0x0A); ps01XferByte(0x10);
    ps01XferByte(0x00 | 0x0B); ps01XferByte(0x10);
    ps01XferByte(0x00 | 0x0C); ps01XferByte(0x10);

    // 6. MAX OCD/STALL thresholds
    t.writeLine("  OCD_TH = 0x1F, STALL_TH = 0x7F");
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
    t.writeLine(buf);
    ps01XferByte(0x00 | 0x1A);
    ps01XferByte((config >> 8) & 0xFF);
    ps01XferByte(config & 0xFF);

    // 9. Final status
    ps01XferByte(0xD0);
    hi = ps01XferByte(0x00);
    lo = ps01XferByte(0x00);
    snprintf(buf, sizeof(buf), "  Final STATUS: 0x%04X", (hi << 8) | lo);
    t.writeLine(buf);

    uint16_t status = (hi << 8) | lo;
    t.writeLine("  Decoded:");
    snprintf(buf, sizeof(buf), "    HiZ=%d BUSY=%d DIR=%d MOT_STATUS=%d",
             (status >> 0) & 1, !((status >> 1) & 1), (status >> 4) & 1,
             (status >> 5) & 3);
    t.writeLine(buf);
    snprintf(buf, sizeof(buf), "    UVLO=%d UVLO_ADC=%d TH_SD=%d TH_WRN=%d",
             !((status >> 9) & 1), !((status >> 10) & 1), !((status >> 11) & 1),
             !((status >> 12) & 1));
    t.writeLine(buf);
    snprintf(buf, sizeof(buf), "    STALL_A=%d STALL_B=%d OCD=%d",
             !((status >> 13) & 1), !((status >> 14) & 1),
             !((status >> 15) & 1));
    t.writeLine(buf);

    ps01ExitBitBang(saved);
    t.writeLine("Reset complete. Try MOVE now.");
}

void BoardDebug::cmdPs01Run(const char* const* args, uint8_t argCount,
                                     Harness::IResponseSink& t) {
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
    t.writeLine(buf);

    uint32_t saved = ps01EnterBitBang();

    // Set KVAL registers
    t.writeLine("  Setting KVAL...");
    ps01XferByte(0x00 | 0x09); ps01XferByte(kval);
    ps01XferByte(0x00 | 0x0A); ps01XferByte(kval);
    ps01XferByte(0x00 | 0x0B); ps01XferByte(kval);
    ps01XferByte(0x00 | 0x0C); ps01XferByte(kval);

    // Set ACC/DEC
    t.writeLine("  Setting ACC/DEC=0x08A...");
    ps01XferByte(0x00 | 0x05); ps01XferByte(0x00); ps01XferByte(0x8A);
    ps01XferByte(0x00 | 0x06); ps01XferByte(0x00); ps01XferByte(0x8A);

    // Set MAX_SPEED
    t.writeLine("  Setting MAX_SPEED=0x41...");
    ps01XferByte(0x00 | 0x07); ps01XferByte(0x00); ps01XferByte(0x41);

    // Clear faults
    ps01XferByte(0xD0);
    uint8_t hi = ps01XferByte(0x00);
    uint8_t lo = ps01XferByte(0x00);
    snprintf(buf, sizeof(buf), "  STATUS before RUN: 0x%04X", (hi << 8) | lo);
    t.writeLine(buf);

    // Send RUN command
    t.writeLine("  Sending RUN command...");
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
    t.writeLine(buf);

    snprintf(buf, sizeof(buf), "    HiZ=%d BUSY=%d MOT_STATUS=%d",
             (status >> 0) & 1, !((status >> 1) & 1), (status >> 5) & 3);
    t.writeLine(buf);

    const char *motStr[] = {"STOPPED", "ACCELERATING", "DECELERATING",
                            "CONST_SPEED"};
    snprintf(buf, sizeof(buf), "    Motion: %s", motStr[(status >> 5) & 3]);
    t.writeLine(buf);

    ps01ExitBitBang(saved);
    t.writeLine("Use PS01_STOP to stop motor.");
}

void BoardDebug::cmdPs01Stop(Harness::IResponseSink& t) {
    t.writeLine("Stopping motor (SoftStop)...");

    uint32_t saved = ps01EnterBitBang();

    ps01XferByte(0xB0); // SoftStop

    for (volatile int d = 0; d < 10000; d++)
      ;
    ps01XferByte(0xD0);
    uint8_t hi = ps01XferByte(0x00);
    uint8_t lo = ps01XferByte(0x00);
    char buf[64];
    snprintf(buf, sizeof(buf), "  STATUS: 0x%04X", (hi << 8) | lo);
    t.writeLine(buf);

    ps01ExitBitBang(saved);
    t.writeLine("Done.");
}

// ============================================================================
// LCD/motor isolation and MOSI tests
// ============================================================================

void BoardDebug::cmdLcdDisable(Harness::IResponseSink& t) {
    char buf[128];
    t.writeLine("Disabling LCD and display task...");


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
    t.writeLine(buf);


    t.writeLine("LCD disabled. SPI1 now exclusively for motor.");
    t.writeLine("Motor commands (MOVE, RUN, etc.) should now work.");
}

void BoardDebug::cmdMosiTest(Harness::IResponseSink& t) {
    char buf[128];
    t.writeLine("MOSI (PA7) toggle test...");
    t.writeLine("Connect scope to PA7 or jumper PA7->PA6 for loopback.");

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
      t.writeLine(buf);
    }

    // Verify PA7 ODR
    GPIOA->BSRR = (1UL << 7);
    int odr_hi = (GPIOA->ODR >> 7) & 1;
    GPIOA->BSRR = (1UL << (7 + 16));
    int odr_lo = (GPIOA->ODR >> 7) & 1;
    snprintf(buf, sizeof(buf), "  PA7 ODR verify: set=%d, clear=%d", odr_hi,
             odr_lo);
    t.writeLine(buf);

    if (odr_hi != 1 || odr_lo != 0) {
      t.writeLine("ERROR: PA7 ODR not responding to BSRR!");
      t.writeLine("GPIO may be in wrong mode or locked.");
    } else {
      t.writeLine("PA7 ODR toggles correctly.");
    }

    GPIOA->MODER = saved_moder;
    SPI1->CR1 |= SPI_CR1_SPE;
    t.writeLine("Done. SPI restored.");
}

void BoardDebug::cmdSpiLoopback(Harness::IResponseSink& t) {
    char buf[128];
    t.writeLine("SPI LOOPBACK TEST");
    t.writeLine("Requires: Jumper wire from PA7 (MOSI) to PA6 (MISO)");
    t.writeLine("Disconnect powerSTEP01 MISO if possible, or ignore motor.");

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
      t.writeLine(buf);
    }

    if (allPass) {
      t.writeLine("LOOPBACK TEST PASSED! MOSI and MISO working.");
    } else {
      t.writeLine("LOOPBACK TEST FAILED. Check jumper connection.");
    }

    GPIOA->MODER = saved_moder;
    SPI1->CR1 |= SPI_CR1_SPE;
    t.writeLine("Done.");
}

// ============================================================================
// SCPI debug queries
// ============================================================================

void BoardDebug::cmdEncDebug(Harness::IResponseSink& t) {
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
    t.writeLine(buf);
}

// ---------------------------------------------------------------------------
// Name table (lets the L3 gate report "locked" without executing anything)
// ---------------------------------------------------------------------------
static const char* const s_rawCommandNames[] = {
    "SPI_DEBUG", "SPI_MODE_TEST", "CS_LOW", "CS_HIGH", "MISO_PULLDOWN", "MISO_NOPULL",
    "STBY_RELEASE", "STBY_HOLD", "STBY_SENSE", "GPIO_STATE", "PA6_HIZ", "PA6_RESTORE", "SPI_STOP",
    "SPI_START", "GPIO_DUMP", "SPI_FLOAT_TEST", "SPI_BITBANG", "SPI_BITBANG_M3",
    "SPI_ECHO_TEST", "SPI_CS_TOGGLE", "PS01_DIAG", "PS01_FIX_CLK", "PS01_SAFE", "PS01_RESET",
    "PS01_RUN", "PS01_STOP", "LCD_DISABLE", "MOSI_TEST", "SPI_LOOPBACK", "ENC_DEBUG",
};

bool BoardDebug::knowsCommand(const char* cmd) const {
    for (const char* name : s_rawCommandNames) {
        if (strcmp(cmd, name) == 0) return true;
    }
    return false;
}

} // namespace Board

// H_L4_L5 init contract
Board::IBoardDebug& Harness::boardDebug()
{
    static Board::BoardDebug s_debug;
    return s_debug;
}
