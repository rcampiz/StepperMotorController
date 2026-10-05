/**
 * @file iresponse_sink.hpp
 * @brief Harness pin: text sink for multi-line command output (shared by L2 and L3)
 *
 * L2 implements it over its transport; L3 (and the board debug commands beneath it)
 * write text through it without ever seeing a transport. Task context only.
 */
#pragma once

namespace Harness {

class IResponseSink {
public:
    virtual ~IResponseSink() = default;
    virtual void write(const char* text) = 0;       ///< no line ending added
    virtual void writeLine(const char* text) = 0;   ///< appends the protocol line ending
};

} // namespace Harness
