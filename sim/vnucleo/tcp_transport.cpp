/**
 * @file tcp_transport.cpp
 * @brief TCP server transport (Winsock on Windows, BSD sockets elsewhere).
 */
#include "tcp_transport.hpp"
#include "harness/pins/oob_estop.hpp"

#include <chrono>
#include <cstring>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
using sock_t = SOCKET;
static void closeSock(long long s) { ::closesocket(static_cast<SOCKET>(s)); }
static bool sockInit() {
    static bool done = false;
    if (!done) {
        WSADATA w;
        done = (::WSAStartup(MAKEWORD(2, 2), &w) == 0);
    }
    return done;
}
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <sys/socket.h>
#  include <unistd.h>
using sock_t = int;
static void closeSock(long long s) { ::close(static_cast<int>(s)); }
static bool sockInit() { return true; }
#endif

namespace Sim {

TcpTransport::TcpTransport(const std::string& bindAddr, uint16_t port)
    : bind_(bindAddr), port_(port) {}

TcpTransport::~TcpTransport() {
    running_ = false;
    if (client_ >= 0) closeSock(client_);
    if (listen_ >= 0) closeSock(listen_);
    if (thread_.joinable()) thread_.detach();
}

bool TcpTransport::listen() {
    if (!sockInit()) return false;
    sock_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (static_cast<long long>(s) < 0) return false;
    int one = 1;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port_);
    a.sin_addr.s_addr = ::inet_addr(bind_.c_str());
    if (::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || ::listen(s, 1) != 0) {
        closeSock(static_cast<long long>(s));
        return false;
    }
    listen_ = static_cast<long long>(s);
    thread_ = std::thread(&TcpTransport::acceptLoop, this);
    return true;
}

void TcpTransport::acceptLoop() {
    uint8_t buf[256];
    while (running_) {
        sock_t c = ::accept(static_cast<sock_t>(listen_.load()), nullptr, nullptr);
        if (static_cast<long long>(c) < 0) {
            if (!running_) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        int one = 1;
        ::setsockopt(c, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
        {
            std::lock_guard<std::mutex> lk(rxMutex_);
            rx_.clear();                                     // a new host: stale bytes are gone
        }
        client_ = static_cast<long long>(c);
        while (running_) {
            int n = ::recv(c, reinterpret_cast<char*>(buf), sizeof(buf), 0);
            if (n <= 0) break;
            std::lock_guard<std::mutex> lk(rxMutex_);
            for (int i = 0; i < n; ++i) {
                if (buf[i] == 0x18) {
                    Harness::invokeOobEstop();           // as the USART2 ISR does on the board
                    continue;
                }
                if (rx_.size() >= RX_RING) {
                    overflows_++;                            // like a full UART ring
                } else {
                    rx_.push_back(buf[i]);
                }
            }
            rxCv_.notify_all();
        }
        {
            std::lock_guard<std::mutex> lk(txMutex_);
            client_ = -1;
        }
        closeSock(static_cast<long long>(c));
    }
}

bool TcpTransport::available() {
    std::lock_guard<std::mutex> lk(rxMutex_);
    return !rx_.empty();
}

size_t TcpTransport::read(uint8_t* buffer, size_t maxLen) {
    std::lock_guard<std::mutex> lk(rxMutex_);
    size_t n = 0;
    while (n < maxLen && !rx_.empty()) {
        buffer[n++] = rx_.front();
        rx_.pop_front();
    }
    return n;
}

bool TcpTransport::readByte(uint8_t& byte, uint32_t timeoutMs) {
    std::unique_lock<std::mutex> lk(rxMutex_);
    if (rx_.empty() && timeoutMs > 0) {
        rxCv_.wait_for(lk, std::chrono::milliseconds(timeoutMs), [this] { return !rx_.empty(); });
    }
    if (rx_.empty()) return false;
    byte = rx_.front();
    rx_.pop_front();
    return true;
}

size_t TcpTransport::write(const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lk(txMutex_);
    long long c = client_.load();
    if (c < 0) return len;                                   // nobody listening: bytes go nowhere
    size_t sent = 0;
    while (sent < len) {
        int n = ::send(static_cast<sock_t>(c), reinterpret_cast<const char*>(data + sent),
                       static_cast<int>(len - sent), 0);
        if (n <= 0) break;
        sent += static_cast<size_t>(n);
    }
    return len;
}

size_t TcpTransport::print(const char* str) {
    return write(reinterpret_cast<const uint8_t*>(str), std::strlen(str));
}

size_t TcpTransport::println(const char* str) {
    size_t n = print(str);
    return n + write(reinterpret_cast<const uint8_t*>("\r\n"), 2);
}

} // namespace Sim
