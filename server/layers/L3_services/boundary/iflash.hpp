/**
 * @file boundary/iflash.hpp
 * @brief Consumer port — Services side of the Services::IFlash harness connection
 *
 * Structurally identical to Services::IFlash.
 * The harness adapter bridges the provider to this consumer port.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

namespace Services {

struct FlashJedecId {
    uint8_t manufacturer;
    uint8_t memoryType;
    uint8_t capacity;
};

class IFlash {
public:
    virtual ~IFlash() = default;

    virtual void read(uint32_t addr, uint8_t* buf, size_t len) = 0;
    virtual void readStart(uint32_t addr, uint8_t* buf, size_t len) = 0;
    virtual void readFinish() = 0;
    virtual void sectorErase(uint32_t addr) = 0;
    virtual void pageProgram(uint32_t addr, const uint8_t* data, size_t len) = 0;
    virtual void blockErase64(uint32_t addr) = 0;
    virtual FlashJedecId readJEDEC() = 0;
    virtual uint32_t capacityBytes() = 0;
};

} // namespace Services
