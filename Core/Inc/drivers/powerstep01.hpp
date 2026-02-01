/**
 * @file powerstep01.hpp
 * @brief powerSTEP01 stepper motor driver for X-NUCLEO-IHM03A1
 */

#ifndef POWERSTEP01_HPP
#define POWERSTEP01_HPP

#include "stm32f401xe.h"
#include "board/board_pins.hpp"
#include "drivers/spi_bus.hpp"

class PowerSTEP01 {
public:
    // Application commands
    enum class Cmd : uint8_t {
        NOP          = 0x00,
        SetParam     = 0x00,  // | register addr
        GetParam     = 0x20,  // | register addr
        Run          = 0x50,  // | direction
        StepClock    = 0x58,  // | direction
        Move         = 0x40,  // | direction
        GoTo         = 0x60,
        GoToDir      = 0x68,  // | direction
        GoUntil      = 0x82,  // | action | direction
        ReleaseSW    = 0x92,  // | action | direction
        GoHome       = 0x70,
        GoMark       = 0x78,
        ResetPos     = 0xD8,
        ResetDevice  = 0xC0,
        SoftStop     = 0xB0,
        HardStop     = 0xB8,
        SoftHiZ      = 0xA0,
        HardHiZ      = 0xA8,
        GetStatus    = 0xD0
    };

    // Register addresses
    enum class Reg : uint8_t {
        ABS_POS      = 0x01,
        EL_POS       = 0x02,
        MARK         = 0x03,
        SPEED        = 0x04,
        ACC          = 0x05,
        DEC          = 0x06,
        MAX_SPEED    = 0x07,
        MIN_SPEED    = 0x08,
        KVAL_HOLD    = 0x09,
        KVAL_RUN     = 0x0A,
        KVAL_ACC     = 0x0B,
        KVAL_DEC     = 0x0C,
        INT_SPEED    = 0x0D,
        ST_SLP       = 0x0E,
        FN_SLP_ACC   = 0x0F,
        FN_SLP_DEC   = 0x10,
        K_THERM      = 0x11,
        ADC_OUT      = 0x12,
        OCD_TH       = 0x13,
        STALL_TH     = 0x14,
        FS_SPD       = 0x15,
        STEP_MODE    = 0x16,
        ALARM_EN     = 0x17,
        GATECFG1     = 0x18,
        GATECFG2     = 0x19,
        CONFIG       = 0x1A,
        STATUS       = 0x1B
    };

    // Status register bits
    struct Status {
        uint16_t raw;
        bool hiZ()       const { return raw & (1 << 0); }
        bool busy()      const { return !(raw & (1 << 1)); }
        bool swOn()      const { return raw & (1 << 2); }
        bool swEvent()   const { return raw & (1 << 3); }
        bool dir()       const { return raw & (1 << 4); }
        uint8_t motStatus() const { return (raw >> 5) & 0x3; }
        bool cmdErr()    const { return raw & (1 << 7); }
        bool stckMod()   const { return raw & (1 << 8); }
        bool uvlo()      const { return !(raw & (1 << 9)); }
        bool uvloADC()   const { return !(raw & (1 << 10)); }
        bool thermalSD() const { return !(raw & (1 << 11)); }
        bool thermalWarn() const { return !(raw & (1 << 12)); }
        bool stallA()    const { return !(raw & (1 << 13)); }
        bool stallB()    const { return !(raw & (1 << 14)); }
        bool ocd()       const { return !(raw & (1 << 15)); }
    };

    PowerSTEP01(SPIBus& spi) : m_spi(spi) {
        initPins();
    }

    void init() {
        // Release standby/reset
        setStandby(false);
        for (volatile int i = 0; i < 10000; i++);  // Brief delay

        // Reset device
        sendCommand(Cmd::ResetDevice);

        // Configure default parameters
        setParam(Reg::ACC, 0x08A);       // Acceleration
        setParam(Reg::DEC, 0x08A);       // Deceleration
        setParam(Reg::MAX_SPEED, 0x41);  // Max speed
        setParam(Reg::KVAL_HOLD, 0x29);  // Holding current
        setParam(Reg::KVAL_RUN, 0x29);   // Running current
        setParam(Reg::KVAL_ACC, 0x29);   // Acceleration current
        setParam(Reg::KVAL_DEC, 0x29);   // Deceleration current
    }

    Status getStatus() {
        Status s;
        m_spi.lock();
        csLow();
        m_spi.transfer(static_cast<uint8_t>(Cmd::GetStatus));
        uint8_t hi = m_spi.transfer(0x00);
        uint8_t lo = m_spi.transfer(0x00);
        csHigh();
        m_spi.unlock();
        s.raw = (hi << 8) | lo;
        return s;
    }

    uint32_t getParam(Reg reg) {
        uint8_t len = paramLen(reg);
        uint32_t val = 0;

        m_spi.lock();
        csLow();
        m_spi.transfer(static_cast<uint8_t>(Cmd::GetParam) | static_cast<uint8_t>(reg));
        for (uint8_t i = 0; i < len; i++) {
            val = (val << 8) | m_spi.transfer(0x00);
        }
        csHigh();
        m_spi.unlock();
        return val;
    }

    void setParam(Reg reg, uint32_t val) {
        uint8_t len = paramLen(reg);

        m_spi.lock();
        csLow();
        m_spi.transfer(static_cast<uint8_t>(Cmd::SetParam) | static_cast<uint8_t>(reg));
        for (int8_t i = len - 1; i >= 0; i--) {
            m_spi.transfer((val >> (8 * i)) & 0xFF);
        }
        csHigh();
        m_spi.unlock();
    }

    void run(bool forward, uint32_t speed) {
        m_spi.lock();
        csLow();
        m_spi.transfer(static_cast<uint8_t>(Cmd::Run) | (forward ? 1 : 0));
        m_spi.transfer((speed >> 16) & 0x0F);
        m_spi.transfer((speed >> 8) & 0xFF);
        m_spi.transfer(speed & 0xFF);
        csHigh();
        m_spi.unlock();
    }

    void move(bool forward, uint32_t steps) {
        m_spi.lock();
        csLow();
        m_spi.transfer(static_cast<uint8_t>(Cmd::Move) | (forward ? 1 : 0));
        m_spi.transfer((steps >> 16) & 0x3F);
        m_spi.transfer((steps >> 8) & 0xFF);
        m_spi.transfer(steps & 0xFF);
        csHigh();
        m_spi.unlock();
    }

    void goTo(int32_t pos) {
        m_spi.lock();
        csLow();
        m_spi.transfer(static_cast<uint8_t>(Cmd::GoTo));
        m_spi.transfer((pos >> 16) & 0x3F);
        m_spi.transfer((pos >> 8) & 0xFF);
        m_spi.transfer(pos & 0xFF);
        csHigh();
        m_spi.unlock();
    }

    void softStop() { sendCommand(Cmd::SoftStop); }
    void hardStop() { sendCommand(Cmd::HardStop); }
    void softHiZ()  { sendCommand(Cmd::SoftHiZ); }
    void hardHiZ()  { sendCommand(Cmd::HardHiZ); }
    void resetPos() { sendCommand(Cmd::ResetPos); }
    void goHome()   { sendCommand(Cmd::GoHome); }
    void goMark()   { sendCommand(Cmd::GoMark); }

    bool isBusy() {
        return (Pins::IHM03A1::BUSY_PORT->IDR & (1 << Pins::IHM03A1::BUSY_PIN)) == 0;
    }

    bool flagActive() {
        return (Pins::IHM03A1::FLAG_PORT->IDR & (1 << Pins::IHM03A1::FLAG_PIN)) == 0;
    }

    void setStandby(bool standby) {
        if (standby) {
            Pins::IHM03A1::STBY_RST_PORT->BSRR = (1 << (Pins::IHM03A1::STBY_RST_PIN + 16));
        } else {
            Pins::IHM03A1::STBY_RST_PORT->BSRR = (1 << Pins::IHM03A1::STBY_RST_PIN);
        }
    }

private:
    SPIBus& m_spi;

    void initPins() {
        // Enable GPIO clocks
        RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;

        // CS pin - output, push-pull, high
        configureOutput(Pins::IHM03A1::CS_PORT, Pins::IHM03A1::CS_PIN);
        csHigh();

        // STBY/RST pin - output, push-pull
        configureOutput(Pins::IHM03A1::STBY_RST_PORT, Pins::IHM03A1::STBY_RST_PIN);
        setStandby(true);  // Start in standby

        // FLAG pin - input with pull-up
        configureInput(Pins::IHM03A1::FLAG_PORT, Pins::IHM03A1::FLAG_PIN);

        // BUSY pin - input with pull-up
        configureInput(Pins::IHM03A1::BUSY_PORT, Pins::IHM03A1::BUSY_PIN);
    }

    void configureOutput(GPIO_TypeDef* port, uint8_t pin) {
        port->MODER &= ~(0x3 << (pin * 2));
        port->MODER |= (0x1 << (pin * 2));   // Output
        port->OTYPER &= ~(1 << pin);          // Push-pull
        port->OSPEEDR |= (0x3 << (pin * 2)); // High speed
    }

    void configureInput(GPIO_TypeDef* port, uint8_t pin) {
        port->MODER &= ~(0x3 << (pin * 2));  // Input mode
        port->PUPDR &= ~(0x3 << (pin * 2));
        port->PUPDR |= (0x1 << (pin * 2));   // Pull-up
    }

    void csLow()  { Pins::IHM03A1::CS_PORT->BSRR = (1 << (Pins::IHM03A1::CS_PIN + 16)); }
    void csHigh() { Pins::IHM03A1::CS_PORT->BSRR = (1 << Pins::IHM03A1::CS_PIN); }

    void sendCommand(Cmd cmd) {
        m_spi.lock();
        csLow();
        m_spi.transfer(static_cast<uint8_t>(cmd));
        csHigh();
        m_spi.unlock();
    }

    uint8_t paramLen(Reg reg) {
        switch (reg) {
            case Reg::ABS_POS:
            case Reg::MARK:
                return 3;
            case Reg::EL_POS:
            case Reg::SPEED:
            case Reg::ACC:
            case Reg::DEC:
            case Reg::INT_SPEED:
            case Reg::CONFIG:
            case Reg::STATUS:
            case Reg::GATECFG1:
                return 2;
            default:
                return 1;
        }
    }
};

#endif // POWERSTEP01_HPP
