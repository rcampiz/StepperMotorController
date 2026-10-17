/**
 * @file tcp_transport.hpp
 * @brief L1 transport for the virtual Nucleo: a TCP server that behaves like USART2 + its
 *        1024-byte RX ring (bytes past a full ring are dropped and counted, F1).
 *
 * One client at a time. A disconnect is the virtual "USB unplug": the board keeps
 * running (its heartbeat watchdog fires if it was armed) and waits for the next client.
 */
#pragma once

#include "L1_transport/boundary/itransport.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace Sim {

class TcpTransport : public Transport::ITransport {
public:
    void setOobEstopEnabled(bool enabled) override { oobEnabled_ = enabled; }
private:
    volatile bool oobEnabled_ = true;
public:
    static constexpr size_t RX_RING = 1024;     // UartHardware RingBuffer<1024>

    TcpTransport(const std::string& bindAddr, uint16_t port);
    ~TcpTransport() override;

    /// Bind and listen; returns false if the port is taken.
    bool listen();
    uint16_t port() const { return port_; }
    bool clientConnected() const { return client_ >= 0; }

    // Transport::ITransport
    bool init() override { return true; }
    bool available() override;
    size_t read(uint8_t* buffer, size_t maxLen) override;
    bool readByte(uint8_t& byte, uint32_t timeoutMs) override;
    size_t write(const uint8_t* data, size_t len) override;
    size_t print(const char* str) override;
    size_t println(const char* str) override;
    void flush() override {}
    bool setBaudRate(uint32_t) override { return true; }     // no line rate on a socket
    uint32_t rxOverflowCount() const override { return overflows_.load(); }

private:
    void acceptLoop();

    std::string bind_;
    uint16_t port_;
    std::atomic<long long> listen_{-1};
    std::atomic<long long> client_{-1};
    std::atomic<bool> running_{true};
    std::atomic<uint32_t> overflows_{0};
    std::thread thread_;
    std::mutex rxMutex_, txMutex_;
    std::condition_variable rxCv_;
    std::deque<uint8_t> rx_;
};

} // namespace Sim
