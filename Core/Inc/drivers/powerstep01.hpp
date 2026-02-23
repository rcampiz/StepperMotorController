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
        // Bits 11-12: TH_STATUS 2-bit field
        // 00=Normal, 01=Warning, 10=Bridge shutdown, 11=Device shutdown
        uint8_t thStatus() const { return (raw >> 11) & 0x3; }
        bool thermalWarn() const { return thStatus() >= 1; }
        bool thermalSD()   const { return thStatus() >= 2; }
        bool ocd()       const { return !(raw & (1 << 13)); }
        bool stallA()    const { return !(raw & (1 << 14)); }
        bool stallB()    const { return !(raw & (1 << 15)); }
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
        for (volatile int i = 0; i < 50000; i++);  // Wait for reset

        // Clear latched faults
        getStatus();

        // Fix CONFIG: Clear EXT_CLK bit (IHM03A1 has no external clock)
        uint32_t config = getParam(Reg::CONFIG);
        config &= ~(1 << 3);  // Clear EXT_CLK bit
        config &= ~(1 << 7);  // Clear OC_SD (OCD won't cause shutdown)
        setParam(Reg::CONFIG, config);

        // Disable problematic alarms (UVLO_ADC from floating ADCIN, thermal false positives)
        // ALARM_EN: disable OCD(0), TH_SD(1), TH_WRN(2), UVLO_ADC(4)
        setParam(Reg::ALARM_EN, 0xE8);

        // Set high thresholds to prevent spurious trips
        setParam(Reg::OCD_TH, 0x1F);    // Max OCD threshold (~10A)
        setParam(Reg::STALL_TH, 0x7F);  // Max stall threshold

        // Disable stall detection by setting FS_SPD to max (full-step speed threshold)
        // When FS_SPD is at max, stall detection is effectively disabled
        setParam(Reg::FS_SPD, 0x3FF);   // Max value, disables stall detection

        // GATECFG2: moderate blanking/dead time
        // TDT[2:0]=011 (1000ns dead time), TBLANK[7:3]=01000 (500ns blanking)
        setParam(Reg::GATECFG2, 0x43);

        // Configure motion parameters
        setParam(Reg::ACC, 0x08A);       // Acceleration
        setParam(Reg::DEC, 0x08A);       // Deceleration
        setParam(Reg::MAX_SPEED, 0x41);  // Max speed

        // KVAL settings for NEMA 23 motor at ~21V supply
        // Increased values for better starting torque
        setParam(Reg::KVAL_HOLD, 0x20);  // Holding current
        setParam(Reg::KVAL_RUN, 0x30);   // Running current
        setParam(Reg::KVAL_ACC, 0x40);   // Acceleration current (higher for startup)
        setParam(Reg::KVAL_DEC, 0x40);   // Deceleration current

        // Clear any faults from initialization
        getStatus();
    }

    Status getStatus() {
        Status s;
        m_spi.lock();
        m_spi.setMode(SPIBus::Mode::Mode3);
        m_spi.setPrescaler(SPIBus::Prescaler::Div32);
        // powerSTEP01 requires CS toggle between each byte!
        transferByte(static_cast<uint8_t>(Cmd::GetStatus));
        uint8_t hi = transferByte(0x00);
        uint8_t lo = transferByte(0x00);
        m_spi.unlock();
        s.raw = (hi << 8) | lo;
        return s;
    }

    uint32_t getParam(Reg reg) {
        uint8_t len = paramLen(reg);
        uint32_t val = 0;

        m_spi.lock();
        m_spi.setMode(SPIBus::Mode::Mode3);
        m_spi.setPrescaler(SPIBus::Prescaler::Div32);
        // powerSTEP01 requires CS toggle between each byte!
        transferByte(static_cast<uint8_t>(Cmd::GetParam) | static_cast<uint8_t>(reg));
        for (uint8_t i = 0; i < len; i++) {
            val = (val << 8) | transferByte(0x00);
        }
        m_spi.unlock();
        return val;
    }

    void setParam(Reg reg, uint32_t val) {
        uint8_t len = paramLen(reg);

        m_spi.lock();
        m_spi.setMode(SPIBus::Mode::Mode3);
        m_spi.setPrescaler(SPIBus::Prescaler::Div32);
        // powerSTEP01 requires CS toggle between each byte!
        transferByte(static_cast<uint8_t>(Cmd::SetParam) | static_cast<uint8_t>(reg));
        for (int8_t i = len - 1; i >= 0; i--) {
            transferByte((val >> (8 * i)) & 0xFF);
        }
        m_spi.unlock();
    }

    void run(bool forward, uint32_t speed) {
        m_spi.lock();
        m_spi.setMode(SPIBus::Mode::Mode3);
        m_spi.setPrescaler(SPIBus::Prescaler::Div32);
        // powerSTEP01 requires CS toggle between each byte!
        transferByte(static_cast<uint8_t>(Cmd::Run) | (forward ? 1 : 0));
        transferByte((speed >> 16) & 0x0F);
        transferByte((speed >> 8) & 0xFF);
        transferByte(speed & 0xFF);
        m_spi.unlock();
    }

    void move(bool forward, uint32_t steps) {
        m_spi.lock();
        m_spi.setMode(SPIBus::Mode::Mode3);
        m_spi.setPrescaler(SPIBus::Prescaler::Div32);
        // powerSTEP01 requires CS toggle per byte
        transferByte(static_cast<uint8_t>(Cmd::Move) | (forward ? 1 : 0));
        transferByte((steps >> 16) & 0x3F);
        transferByte((steps >> 8) & 0xFF);
        transferByte(steps & 0xFF);
        m_spi.unlock();
    }

    void goTo(int32_t pos) {
        m_spi.lock();
        m_spi.setMode(SPIBus::Mode::Mode3);
        m_spi.setPrescaler(SPIBus::Prescaler::Div32);
        // powerSTEP01 requires CS toggle per byte
        transferByte(static_cast<uint8_t>(Cmd::GoTo));
        transferByte((pos >> 16) & 0x3F);
        transferByte((pos >> 8) & 0xFF);
        transferByte(pos & 0xFF);
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

    /**
     * @brief Transfer one byte with CS toggle (required by powerSTEP01)
     *
     * powerSTEP01 requires CS to go HIGH for at least 625ns between each byte.
     * This method handles the CS low -> transfer -> CS high -> delay sequence.
     */
    uint8_t transferByte(uint8_t data) {
        csLow();
        for (volatile int d = 0; d < 10; d++);  // Setup time
        uint8_t rx = m_spi.transfer(data);
        csHigh();
        // Minimum 625ns CS high time. At 84MHz, 100 cycles ≈ 1.2µs
        for (volatile int d = 0; d < 100; d++);
        return rx;
    }

    void sendCommand(Cmd cmd) {
        m_spi.lock();
        m_spi.setMode(SPIBus::Mode::Mode3);
        m_spi.setPrescaler(SPIBus::Prescaler::Div32);
        // powerSTEP01 requires CS toggle per byte
        transferByte(static_cast<uint8_t>(cmd));
        m_spi.unlock();
    }

    uint8_t paramLen(Reg reg) {
        switch (reg) {
            case Reg::ABS_POS:
            case Reg::MARK:
            case Reg::SPEED:  // 20-bit register
                return 3;
            case Reg::EL_POS:
            case Reg::ACC:
            case Reg::DEC:
            case Reg::MAX_SPEED:
            case Reg::MIN_SPEED:
            case Reg::FS_SPD:
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
