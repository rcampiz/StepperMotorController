/**
 * @file metered_taps.hpp
 * @brief Always-on meter taps: decorators that count each harness crossing (calls, bytes,
 *        time inside, crossing task) into the harness meter and forward unchanged.
 *
 * Applied only in harness_connect.cpp, in every build. In debug builds the Traced* taps
 * (interface trace) sit on top of these: metering and tracing are separate decorators,
 * both orthogonal to the layers. Every method of each interface is forwarded, the
 * defaulted ones too (a missing forward would silently fall back to the interface default).
 */
#pragma once

#include "harness/trace/harness_meter.hpp"
#include "L5_board/boundary/ispi_bus.hpp"
#include "L1_transport/boundary/itransport.hpp"
#include "L1_transport/boundary/ibyte_channel.hpp"
#include "L4_drivers/boundary/imotor_driver.hpp"
#include "L4_drivers/boundary/iencoder_hardware.hpp"
#include "L4_drivers/boundary/icanvas.hpp"
#include "L4_drivers/boundary/iflash.hpp"
#include "L2_protocol/boundary/icommand_processor.hpp"

// Each decorator is a counter plus one forward. At -O2 GCC would speculatively devirtualize
// the forward (one implementation per interface) and inline the whole driver method into
// every wrapper: ~24 KB of flash. Keep the forwards plain virtual calls.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize("no-devirtualize-speculatively", "no-devirtualize")
#endif

namespace Harness {

// ----------------------------------------------------------------------------------------
// H_L4_L5: an SPI bus. One crossing = one lock..unlock session; busy = the time the bus is
// held; bytes = every byte clocked (single transfers included).
// ----------------------------------------------------------------------------------------
class MeteredSPIBus : public Board::ISPIBus {
public:
    MeteredSPIBus(Board::ISPIBus& real, Meter::Net net) : m_real(real), m_net(net) {}

    void lock() override { m_real.lock(); m_t0 = Meter::nowUs(); }
    void unlock() override
    {
        const uint32_t held = Meter::nowUs() - m_t0;
        Meter::count(m_net, 0, held);
        m_real.unlock();
    }
    void setMode(Board::SPIMode mode) override { m_real.setMode(mode); }
    Board::SPIMode getMode() const override { return m_real.getMode(); }
    void setPrescaler(Board::SPIPrescaler p) override { m_real.setPrescaler(p); }
    uint8_t transfer(uint8_t data) override { Meter::addBytes(m_net, 1); return m_real.transfer(data); }
    void transfer(const uint8_t* tx, uint8_t* rx, size_t len) override
    {
        Meter::addBytes(m_net, static_cast<uint32_t>(len));
        m_real.transfer(tx, rx, len);
    }
    void writeOnly(const uint8_t* data, size_t len) override
    {
        Meter::addBytes(m_net, static_cast<uint32_t>(len));
        m_real.writeOnly(data, len);
    }
    void startAsyncRead(uint8_t* data, size_t len) override
    {
        Meter::addBytes(m_net, static_cast<uint32_t>(len));
        m_real.startAsyncRead(data, len);
    }
    void waitAsyncRead() override { m_real.waitAsyncRead(); }
    void writeFill(const uint8_t* pattern, size_t patternLen, uint32_t repeatCount) override
    {
        Meter::addBytes(m_net, static_cast<uint32_t>(patternLen * repeatCount));
        m_real.writeFill(pattern, patternLen, repeatCount);
    }

private:
    Board::ISPIBus& m_real;
    Meter::Net m_net;
    uint32_t m_t0 = 0;   ///< lock time (one holder at a time)
};

// ----------------------------------------------------------------------------------------
// H_L1_L2: the host transport. Crossings with data only (polls for nothing are not counted).
// ----------------------------------------------------------------------------------------
class MeteredTransport : public Transport::ITransport {
public:
    explicit MeteredTransport(Transport::ITransport& real) : m_real(real) {}

    bool init() override { return m_real.init(); }
    bool available() override { return m_real.available(); }
    size_t read(uint8_t* buffer, size_t maxLen) override
    {
        const size_t n = m_real.read(buffer, maxLen);
        if (n != 0u) Meter::count(Meter::L1_L2_TRANSPORT, static_cast<uint32_t>(n));
        return n;
    }
    bool readByte(uint8_t& byte, uint32_t timeoutMs) override
    {
        const bool ok = m_real.readByte(byte, timeoutMs);
        if (ok) Meter::count(Meter::L1_L2_TRANSPORT, 1);
        return ok;
    }
    size_t write(const uint8_t* data, size_t len) override
    {
        Meter::Scope m(Meter::L1_L2_TRANSPORT, static_cast<uint32_t>(len));
        return m_real.write(data, len);
    }
    size_t print(const char* str) override
    {
        const size_t n = m_real.print(str);
        Meter::count(Meter::L1_L2_TRANSPORT, static_cast<uint32_t>(n));
        return n;
    }
    size_t println(const char* str) override
    {
        const size_t n = m_real.println(str);
        Meter::count(Meter::L1_L2_TRANSPORT, static_cast<uint32_t>(n));
        return n;
    }
    void flush() override { m_real.flush(); }
    bool setBaudRate(uint32_t baud) override { return m_real.setBaudRate(baud); }
    uint32_t baudRate() const override { return m_real.baudRate(); }
    uint32_t rxOverflowCount() const override { return m_real.rxOverflowCount(); }
    uint32_t lastRxTickUs() const override { return m_real.lastRxTickUs(); }

private:
    Transport::ITransport& m_real;
};

// ----------------------------------------------------------------------------------------
// X_L5_L1: host bytes from the board's channel (ISR-filled ring) up to L1
// ----------------------------------------------------------------------------------------
class MeteredByteChannel : public Transport::IByteChannel {
public:
    explicit MeteredByteChannel(Transport::IByteChannel& real) : m_real(real) {}

    bool init() override { return m_real.init(); }
    bool hasData() override { return m_real.hasData(); }
    size_t read(uint8_t* buffer, size_t maxLen) override
    {
        const size_t n = m_real.read(buffer, maxLen);
        if (n != 0u) Meter::count(Meter::L5_L1_BYTES, static_cast<uint32_t>(n));
        return n;
    }
    size_t write(const uint8_t* data, size_t len) override
    {
        Meter::count(Meter::L5_L1_BYTES, static_cast<uint32_t>(len));
        return m_real.write(data, len);
    }
    void flush() override { m_real.flush(); }
    bool setBaudRate(uint32_t baud) override { return m_real.setBaudRate(baud); }
    uint32_t baudRate() const override { return m_real.baudRate(); }
    uint32_t rxOverflowCount() const override { return m_real.rxOverflowCount(); }
    uint32_t lastRxTickUs() const override { return m_real.lastRxTickUs(); }

private:
    Transport::IByteChannel& m_real;
};

// ----------------------------------------------------------------------------------------
// H_L3_L4: the motor driver. Every call is an SPI exchange with the chip.
// ----------------------------------------------------------------------------------------
class MeteredMotorDriver : public Drivers::IMotorDriver {
public:
    explicit MeteredMotorDriver(Drivers::IMotorDriver& real) : m_real(real) {}

    void init() override { Meter::Scope m(N); m_real.init(); }
    void run(bool forward, float fullStepsPerSec) override { Meter::Scope m(N); m_real.run(forward, fullStepsPerSec); }
    void move(bool forward, uint32_t microsteps) override { Meter::Scope m(N); m_real.move(forward, microsteps); }
    void goTo(int32_t positionMicrosteps) override { Meter::Scope m(N); m_real.goTo(positionMicrosteps); }
    void softStop() override { Meter::Scope m(N); m_real.softStop(); }
    void hardStop() override { Meter::Scope m(N); m_real.hardStop(); }
    void softHiZ() override { Meter::Scope m(N); m_real.softHiZ(); }
    void hardHiZ() override { Meter::Scope m(N); m_real.hardHiZ(); }
    void goHome() override { Meter::Scope m(N); m_real.goHome(); }
    void goMark() override { Meter::Scope m(N); m_real.goMark(); }
    void resetPos() override { Meter::Scope m(N); m_real.resetPos(); }
    void setMaxSpeed(float fullStepsPerSec) override { Meter::Scope m(N); m_real.setMaxSpeed(fullStepsPerSec); }
    float getMaxSpeed() override { Meter::Scope m(N); return m_real.getMaxSpeed(); }
    void setMark(int32_t positionMicrosteps) override { Meter::Scope m(N); m_real.setMark(positionMicrosteps); }
    void setAcceleration(float fsps2) override { Meter::Scope m(N); m_real.setAcceleration(fsps2); }
    void setDeceleration(float fsps2) override { Meter::Scope m(N); m_real.setDeceleration(fsps2); }
    // Pure calls (no bus traffic, any task): forwarded, not counted
    const Drivers::DriverCaps& caps() const override { return m_real.caps(); }
    Drivers::DriverSettings quantize(const Drivers::DriverSettings& s) const override { return m_real.quantize(s); }
    bool toLegacyUnits(const Drivers::DriverSettings& s, Drivers::LegacyDriverUnits& out) const override { return m_real.toLegacyUnits(s, out); }
    bool fromLegacyUnits(const Drivers::LegacyDriverUnits& in, Drivers::DriverSettings& s) const override { return m_real.fromLegacyUnits(in, s); }
    Drivers::ApplyResult apply(const Drivers::DriverSettings& s) override { Meter::Scope m(N); return m_real.apply(s); }
    bool readSettings(Drivers::DriverSettings& inForce) override { Meter::Scope m(N); return m_real.readSettings(inForce); }
    bool checkReset() override { Meter::Scope m(N); return m_real.checkReset(); }
    bool setRunCurrentLive(float amps) override { Meter::Scope m(N); return m_real.setRunCurrentLive(amps); }
    size_t diagnostics(Harness::DriverDiagView view, char* buf, size_t size) override { Meter::Scope m(N); return m_real.diagnostics(view, buf, size); }
    Drivers::MotorStatus getStatus() override { Meter::Scope m(N); return m_real.getStatus(); }
    Drivers::MotorStatus readState() override { Meter::Scope m(N); return m_real.readState(); }
    bool isBusy() override { return m_real.isBusy(); }              // a pin read, not a crossing worth counting
    bool flagActive() override { return m_real.flagActive(); }
    void setStandby(bool standby) override { m_real.setStandby(standby); }   // ISR-safe GPIO write: untouched

private:
    static constexpr Meter::Net N = Meter::L3_L4_MOTOR;
    Drivers::IMotorDriver& m_real;
};

// ----------------------------------------------------------------------------------------
// H_L3_L4: the encoder hardware (reads of the counter / DMA ring; the index ISR untouched)
// ----------------------------------------------------------------------------------------
class MeteredEncoder : public Drivers::IEncoderHardware {
public:
    explicit MeteredEncoder(Drivers::IEncoderHardware& real) : m_real(real) {}

    bool isReady() const override { return m_real.isReady(); }
    int32_t getCount() const override { Meter::count(N); return m_real.getCount(); }
    void reset() override { Meter::count(N); m_real.reset(); }
    Drivers::IndexSnapshot readIndex() const override { Meter::count(N); return m_real.readIndex(); }
    void indexISR(uint32_t tickMs, uint32_t tickUs) override { m_real.indexISR(tickMs, tickUs); }
    const uint16_t* sampleBuffer() const override { return m_real.sampleBuffer(); }
    uint32_t sampleBufferSize() const override { return m_real.sampleBufferSize(); }
    uint32_t sampleWriteIndex() const override { Meter::count(N); return m_real.sampleWriteIndex(); }
    uint16_t sampleRateHz() const override { return m_real.sampleRateHz(); }
    void restartSampling(uint16_t hz) override { Meter::count(N); m_real.restartSampling(hz); }
    void pauseSampling() override { Meter::count(N); m_real.pauseSampling(); }
    const uint16_t* syncBuffer() const override { return m_real.syncBuffer(); }

private:
    static constexpr Meter::Net N = Meter::L3_L4_ENCODER;
    Drivers::IEncoderHardware& m_real;
};

// ----------------------------------------------------------------------------------------
// H_L3_L4: the LCD canvas. bytes = pixel bytes (2 a pixel) or stream bytes sent.
// ----------------------------------------------------------------------------------------
class MeteredCanvas : public Drivers::ICanvas {
public:
    explicit MeteredCanvas(Drivers::ICanvas& real) : m_real(real) {}

    void fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) override
    {
        Meter::Scope m(N, static_cast<uint32_t>(w) * h * 2u);
        m_real.fillRect(x, y, w, h, color);
    }
    void drawPixel(uint16_t x, uint16_t y, uint16_t color) override { Meter::Scope m(N, 2); m_real.drawPixel(x, y, color); }
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) override
    {
        Meter::Scope m(N);
        m_real.drawLine(x0, y0, x1, y1, color);
    }
    void drawChar(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale = 1) override
    {
        Meter::Scope m(N);
        m_real.drawChar(x, y, c, fg, bg, scale);
    }
    void drawString(uint16_t x, uint16_t y, const char* str, uint16_t fg, uint16_t bg, uint8_t scale = 1) override
    {
        Meter::Scope m(N);
        m_real.drawString(x, y, str, fg, bg, scale);
    }
    void drawInt(uint16_t x, uint16_t y, int32_t value, uint8_t fieldWidth, uint16_t fg, uint16_t bg,
                 uint8_t scale = 1) override
    {
        Meter::Scope m(N);
        m_real.drawInt(x, y, value, fieldWidth, fg, bg, scale);
    }
    void drawUInt(uint16_t x, uint16_t y, uint32_t value, uint8_t fieldWidth, uint16_t fg, uint16_t bg,
                  uint8_t scale = 1) override
    {
        Meter::Scope m(N);
        m_real.drawUInt(x, y, value, fieldWidth, fg, bg, scale);
    }
    void blitTextLine(uint16_t x, uint16_t y, uint16_t lineW, uint16_t lineH, const char* text, uint16_t fg,
                      uint16_t bg) override
    {
        Meter::Scope m(N, static_cast<uint32_t>(lineW) * lineH * 2u);
        m_real.blitTextLine(x, y, lineW, lineH, text, fg, bg);
    }
    void blitTextLineColored(uint16_t x, uint16_t y, uint16_t lineW, uint16_t lineH, const char* text,
                             const uint16_t* charColors, uint16_t bg) override
    {
        Meter::Scope m(N, static_cast<uint32_t>(lineW) * lineH * 2u);
        m_real.blitTextLineColored(x, y, lineW, lineH, text, charColors, bg);
    }
    void drawBitmapRaw(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t* data, size_t len) override
    {
        Meter::Scope m(N, static_cast<uint32_t>(len));
        m_real.drawBitmapRaw(x, y, w, h, data, len);
    }
    void blitColumn(uint16_t x, uint16_t y, uint16_t h, const uint8_t* data) override
    {
        Meter::Scope m(N, static_cast<uint32_t>(h) * 2u);
        m_real.blitColumn(x, y, h, data);
    }
    // A stream counts as one crossing from Start to End; its data adds bytes and time
    void streamBitmapStart(uint16_t x, uint16_t y, uint16_t w, uint16_t h) override
    {
        m_streamT0 = Meter::nowUs();
        m_real.streamBitmapStart(x, y, w, h);
    }
    void streamBitmapData(const uint8_t* data, size_t len) override
    {
        Meter::addBytes(N, static_cast<uint32_t>(len));
        m_real.streamBitmapData(data, len);
    }
    void streamBitmapEnd() override
    {
        m_real.streamBitmapEnd();
        Meter::count(N, 0, Meter::nowUs() - m_streamT0);
    }
    bool isStreaming() const override { return m_real.isStreaming(); }
    void drawTestPattern() override { Meter::Scope m(N); m_real.drawTestPattern(); }
    void waitForVSync() override { m_real.waitForVSync(); }          // waiting on the panel, not a crossing
    bool isRotated180() const override { return m_real.isRotated180(); }
    bool writePanelRegister(uint8_t reg, const uint8_t* data, uint8_t len) override
    {
        Meter::Scope m(N, len);
        return m_real.writePanelRegister(reg, data, len);
    }
    const uint8_t* mediumGlyphRows(char c) const override { return m_real.mediumGlyphRows(c); }   // font lookup

private:
    static constexpr Meter::Net N = Meter::L3_L4_CANVAS;
    Drivers::ICanvas& m_real;
    uint32_t m_streamT0 = 0;
};

// ----------------------------------------------------------------------------------------
// H_L3_L4: the NOR flash (image slots, device identity)
// ----------------------------------------------------------------------------------------
class MeteredFlash : public Drivers::IFlash {
public:
    explicit MeteredFlash(Drivers::IFlash& real) : m_real(real) {}

    void read(uint32_t addr, uint8_t* buf, size_t len) override
    {
        Meter::Scope m(N, static_cast<uint32_t>(len));
        m_real.read(addr, buf, len);
    }
    void readStart(uint32_t addr, uint8_t* buf, size_t len) override
    {
        Meter::count(N, static_cast<uint32_t>(len));
        m_real.readStart(addr, buf, len);
    }
    void readFinish() override { m_real.readFinish(); }
    void sectorErase(uint32_t addr) override { Meter::Scope m(N); m_real.sectorErase(addr); }
    void pageProgram(uint32_t addr, const uint8_t* data, size_t len) override
    {
        Meter::Scope m(N, static_cast<uint32_t>(len));
        m_real.pageProgram(addr, data, len);
    }
    void blockErase64(uint32_t addr) override { Meter::Scope m(N); m_real.blockErase64(addr); }
    Drivers::FlashJedecId readJEDEC() override { Meter::Scope m(N); return m_real.readJEDEC(); }
    uint32_t capacityBytes() override { return m_real.capacityBytes(); }

private:
    static constexpr Meter::Net N = Meter::L3_L4_FLASH;
    Drivers::IFlash& m_real;
};

// ----------------------------------------------------------------------------------------
// H_L2_L3 (up): L3's CommsService driving the L2 processor: parse input, telemetry, events
// ----------------------------------------------------------------------------------------
class MeteredCommandProcessor : public Protocol::ICommandProcessor {
public:
    explicit MeteredCommandProcessor(Protocol::ICommandProcessor& real) : m_real(real) {}

    void process() override { Meter::Scope m(N); m_real.process(); }
    void checkBaudRevert() override { m_real.checkBaudRevert(); }
    void emitBanner() override { Meter::Scope m(N); m_real.emitBanner(); }
    void emitEvent(const AsyncEvent& evt, uint32_t seq, uint32_t tsMs) override
    {
        Meter::Scope m(N);
        m_real.emitEvent(evt, seq, tsMs);
    }
    void emitTelemetry(const Protocol::TelemetrySnapshot& snap) override
    {
        Meter::Scope m(N);
        m_real.emitTelemetry(snap);
    }
    void emitJoystick(UI::JoyDirection dir, bool pressed) override
    {
        Meter::Scope m(N);
        m_real.emitJoystick(dir, pressed);
    }
    void getDispatchStats(Harness::DispatchStats& out) const override { m_real.getDispatchStats(out); }

private:
    static constexpr Meter::Net N = Meter::L3_L2_PROCESSOR;
    Protocol::ICommandProcessor& m_real;
};

} // namespace Harness

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
