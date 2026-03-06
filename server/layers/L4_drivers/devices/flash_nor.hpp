/**
 * @file flash.hpp
 * @brief SPI NOR Flash driver for X-NUCLEO-GFX01M2
 *
 * CS pin is injected via constructor — no CMSIS dependency.
 */

#ifndef FLASH_HPP
#define FLASH_HPP

#include "harness/pins/igpio.hpp"
#include "harness/pins/ispi_bus.hpp"
#include "harness/pins/iflash.hpp"

namespace Drivers {

class SPIFlash : public Harness::IFlash {
public:
    // Standard SPI flash commands
    enum class Cmd : uint8_t {
        ReadJEDEC     = 0x9F,
        ReadStatus1   = 0x05,
        ReadStatus2   = 0x35,
        WriteEnable   = 0x06,
        WriteDisable  = 0x04,
        WriteStatusReg = 0x01,
        Read          = 0x03,
        FastRead      = 0x0B,
        PageProgram   = 0x02,
        SectorErase   = 0x20,  // 4KB
        BlockErase32  = 0x52,  // 32KB
        BlockErase64  = 0xD8,  // 64KB
        ChipErase     = 0xC7,
        PowerDown     = 0xB9,
        ReleasePD     = 0xAB,
        ReadUID       = 0x4B
    };

    using JEDEC_ID = Harness::FlashJedecId;

    /**
     * @brief Construct with SPI bus and pre-configured CS GPIO
     *
     * CS pin must be configured as output (high) before construction.
     */
    SPIFlash(Harness::ISPIBus& spi, Harness::IGPIO& cs) : m_spi(spi), m_cs(cs) {
        csHigh();
    }

    void init() {
        releasePowerDown();
        clearWriteProtection();
    }

    /**
     * @brief Clear all block protect bits (BP0-3) in status register.
     *
     * Some flash chips ship with or retain write protection across power
     * cycles.  This ensures all sectors are writable.
     */
    void clearWriteProtection() {
        uint8_t sr = readStatus1();
        if ((sr & 0x3C) != 0) {  // BP0-BP3 = bits 2-5
            writeEnable();
            m_spi.lock();
            csLow();
            m_spi.transfer(static_cast<uint8_t>(Cmd::WriteStatusReg));
            m_spi.transfer(sr & ~0x3C);  // Clear BP bits, keep other bits
            csHigh();
            m_spi.unlock();
            waitReady();
        }
    }

    /**
     * @brief Check if write-enable latch (WEL) is set after WREN.
     * @return true if WEL bit is set.
     */
    bool isWriteEnabled() {
        return readStatus1() & 0x02;
    }

    Harness::FlashJedecId readJEDEC() override {
        JEDEC_ID id;
        m_spi.lock();
        csLow();
        m_spi.transfer(static_cast<uint8_t>(Cmd::ReadJEDEC));
        id.manufacturer = m_spi.transfer(0xFF);
        id.memoryType   = m_spi.transfer(0xFF);
        id.capacity     = m_spi.transfer(0xFF);
        csHigh();
        m_spi.unlock();
        return id;
    }

    uint8_t readStatus1() {
        m_spi.lock();
        csLow();
        m_spi.transfer(static_cast<uint8_t>(Cmd::ReadStatus1));
        uint8_t status = m_spi.transfer(0xFF);
        csHigh();
        m_spi.unlock();
        return status;
    }

    bool isBusy() {
        return readStatus1() & 0x01;
    }

    void waitReady() {
        while (isBusy());
    }

    void read(uint32_t addr, uint8_t* buf, size_t len) override {
        m_spi.lock();
        csLow();
        m_spi.transfer(static_cast<uint8_t>(Cmd::FastRead));
        m_spi.transfer((addr >> 16) & 0xFF);
        m_spi.transfer((addr >> 8) & 0xFF);
        m_spi.transfer(addr & 0xFF);
        m_spi.transfer(0xFF);  // Dummy byte required by Fast Read (0x0B)
        m_spi.read(buf, len);
        csHigh();
        m_spi.unlock();
    }

    /**
     * @brief Start non-blocking flash read (sends command, starts DMA, returns)
     *
     * Bus lock and CS are held until readFinish() is called.
     * Caller must call readFinish() before using the data.
     */
    void readStart(uint32_t addr, uint8_t* buf, size_t len) override {
        m_spi.lock();
        csLow();
        m_spi.transfer(static_cast<uint8_t>(Cmd::FastRead));
        m_spi.transfer((addr >> 16) & 0xFF);
        m_spi.transfer((addr >> 8) & 0xFF);
        m_spi.transfer(addr & 0xFF);
        m_spi.transfer(0xFF);  // Dummy byte
        m_spi.startAsyncRead(buf, len);
    }

    /**
     * @brief Finish non-blocking flash read (waits for DMA, releases CS and bus lock)
     */
    void readFinish() override {
        m_spi.waitAsyncRead();
        csHigh();
        m_spi.unlock();
    }

    void writeEnable() {
        m_spi.lock();
        csLow();
        m_spi.transfer(static_cast<uint8_t>(Cmd::WriteEnable));
        csHigh();
        m_spi.unlock();
    }

    void pageProgram(uint32_t addr, const uint8_t* data, size_t len) override {
        writeEnable();
        m_spi.lock();
        csLow();
        m_spi.transfer(static_cast<uint8_t>(Cmd::PageProgram));
        m_spi.transfer((addr >> 16) & 0xFF);
        m_spi.transfer((addr >> 8) & 0xFF);
        m_spi.transfer(addr & 0xFF);
        m_spi.write(data, len);
        csHigh();
        m_spi.unlock();
        waitReady();
    }

    void sectorErase(uint32_t addr) override {
        writeEnable();
        m_spi.lock();
        csLow();
        m_spi.transfer(static_cast<uint8_t>(Cmd::SectorErase));
        m_spi.transfer((addr >> 16) & 0xFF);
        m_spi.transfer((addr >> 8) & 0xFF);
        m_spi.transfer(addr & 0xFF);
        csHigh();
        m_spi.unlock();
        waitReady();
    }

    void powerDown() {
        m_spi.lock();
        csLow();
        m_spi.transfer(static_cast<uint8_t>(Cmd::PowerDown));
        csHigh();
        m_spi.unlock();
    }

    void releasePowerDown() {
        m_spi.lock();
        csLow();
        m_spi.transfer(static_cast<uint8_t>(Cmd::ReleasePD));
        csHigh();
        m_spi.unlock();
        // Wait tRES1 (3us typical)
        for (volatile int i = 0; i < 100; i++);
    }

    /**
     * @brief Get flash capacity in bytes from JEDEC ID
     * @return Capacity in bytes, or 0 if unknown
     */
    uint32_t capacityBytes() override {
        JEDEC_ID id = readJEDEC();
        // Capacity byte: 0x14=1MB, 0x15=2MB, 0x16=4MB, 0x17=8MB, 0x18=16MB
        if (id.capacity >= 0x10 && id.capacity <= 0x20) {
            return 1UL << id.capacity;
        }
        return 0;
    }

    /**
     * @brief Erase a 64KB block at the given address
     * @param addr Address within the block (will be aligned to 64KB boundary)
     */
    void blockErase64(uint32_t addr) override {
        writeEnable();
        m_spi.lock();
        csLow();
        m_spi.transfer(static_cast<uint8_t>(Cmd::BlockErase64));
        m_spi.transfer((addr >> 16) & 0xFF);
        m_spi.transfer((addr >> 8) & 0xFF);
        m_spi.transfer(addr & 0xFF);
        csHigh();
        m_spi.unlock();
        waitReady();
    }

    /**
     * @brief Erase the entire chip
     * WARNING: This can take several seconds to complete.
     */
    void chipErase() {
        writeEnable();
        m_spi.lock();
        csLow();
        m_spi.transfer(static_cast<uint8_t>(Cmd::ChipErase));
        csHigh();
        m_spi.unlock();
        waitReady();
    }

private:
    Harness::ISPIBus& m_spi;
    Harness::IGPIO& m_cs;

    void csLow()  { m_cs.write(false); }
    void csHigh() { m_cs.write(true); }
};

} // namespace Drivers

#endif // FLASH_HPP
