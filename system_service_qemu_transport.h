#pragma once

#include "system_service_protocol.h"

#include <chrono>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#if defined(_MSC_VER)
#pragma comment(lib, "ws2_32.lib")
#endif
#else
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace gxos {
namespace system_service {

constexpr uint16_t kDefaultQemuCom2Port = 17771;

struct QemuCom2Endpoint {
    uint16_t port{kDefaultQemuCom2Port};
};

namespace detail {

#if defined(_WIN32)
using ServiceSocket = SOCKET;
constexpr ServiceSocket kInvalidServiceSocket = INVALID_SOCKET;
inline int closeServiceSocket(ServiceSocket socket) { return closesocket(socket); }
inline bool setServiceSocketNonBlocking(ServiceSocket socket)
{
    u_long enabled = 1;
    return ioctlsocket(socket, FIONBIO, &enabled) == 0;
}
inline int lastServiceSocketError() { return WSAGetLastError(); }
inline bool isWouldBlock(int error)
{
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS || error == WSAEALREADY;
}
#else
using ServiceSocket = int;
constexpr ServiceSocket kInvalidServiceSocket = -1;
inline int closeServiceSocket(ServiceSocket socket) { return close(socket); }
inline bool setServiceSocketNonBlocking(ServiceSocket socket)
{
    const int flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
}
inline int lastServiceSocketError() { return errno; }
inline bool isWouldBlock(int error)
{
    return error == EWOULDBLOCK || error == EINPROGRESS || error == EALREADY;
}
#endif

class SocketSession {
public:
    SocketSession()
    {
#if defined(_WIN32)
        WSADATA data{};
        m_ready = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
        m_ready = true;
#endif
    }
    ~SocketSession()
    {
#if defined(_WIN32)
        if (m_ready) WSACleanup();
#endif
    }
    bool ready() const { return m_ready; }
private:
    bool m_ready{false};
};

inline bool waitForSocket(ServiceSocket socket, bool writeReady,
                          std::chrono::steady_clock::time_point deadline)
{
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) return false;
    const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(deadline - now);
    timeval timeout{};
    timeout.tv_sec = static_cast<long>(remaining.count() / 1000000);
    timeout.tv_usec = static_cast<long>(remaining.count() % 1000000);
    fd_set readSet;
    fd_set writeSet;
    FD_ZERO(&readSet);
    FD_ZERO(&writeSet);
    if (writeReady) FD_SET(socket, &writeSet);
    else FD_SET(socket, &readSet);
#if defined(_WIN32)
    const int selected = select(0, writeReady ? nullptr : &readSet,
        writeReady ? &writeSet : nullptr, nullptr, &timeout);
#else
    const int selected = select(socket + 1, writeReady ? nullptr : &readSet,
        writeReady ? &writeSet : nullptr, nullptr, &timeout);
#endif
    return selected > 0;
}

inline bool transferAll(ServiceSocket socket, uint8_t* bytes, size_t count,
                        bool sending, std::chrono::steady_clock::time_point deadline,
                        bool& disconnected)
{
    size_t transferred = 0;
    while (transferred < count) {
        if (!waitForSocket(socket, sending, deadline)) return false;
        const size_t remaining = count - transferred;
#if defined(_WIN32)
        const int amount = sending
            ? send(socket, reinterpret_cast<const char*>(bytes + transferred),
                static_cast<int>(remaining), 0)
            : recv(socket, reinterpret_cast<char*>(bytes + transferred),
                static_cast<int>(remaining), 0);
#else
        const ssize_t amount = sending
            ? send(socket, bytes + transferred, remaining, 0)
            : recv(socket, bytes + transferred, remaining, 0);
#endif
        if (amount == 0) { disconnected = true; return false; }
        if (amount < 0) {
            const int error = lastServiceSocketError();
            if (isWouldBlock(error)) continue;
            disconnected = true;
            return false;
        }
        transferred += static_cast<size_t>(amount);
    }
    return true;
}

} // namespace detail

class QemuCom2TcpTransport {
public:
    explicit QemuCom2TcpTransport(uint16_t port = kDefaultQemuCom2Port)
    {
        m_endpoint.port = port;
    }

    Transport asTransport()
    {
        return Transport{ this, &QemuCom2TcpTransport::transact };
    }

    uint16_t port() const { return m_endpoint.port; }

private:
    static TransportResult transact(void* context,
                                    const uint8_t* request,
                                    size_t requestBytes,
                                    uint8_t* response,
                                    size_t responseCapacity,
                                    size_t* responseBytes,
                                    uint32_t timeoutMs)
    {
        if (responseBytes) *responseBytes = 0;
        if (!context || !request || requestBytes < kRequestHeaderBytes ||
            requestBytes > kMaxRequestBytes ||
            !response || !responseBytes || responseCapacity < kResponseHeaderBytes)
            return TransportResult::Failed;
        const QemuCom2Endpoint& endpoint =
            static_cast<const QemuCom2TcpTransport*>(context)->m_endpoint;
        if (endpoint.port == 0u) return TransportResult::Failed;

        detail::SocketSession session;
        if (!session.ready()) return TransportResult::Failed;
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(timeoutMs);
        detail::ServiceSocket socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket == detail::kInvalidServiceSocket) return TransportResult::Failed;
        struct SocketGuard {
            detail::ServiceSocket value;
            ~SocketGuard() { if (value != detail::kInvalidServiceSocket) detail::closeServiceSocket(value); }
        } guard{ socket };
        if (!detail::setServiceSocketNonBlocking(socket)) return TransportResult::Failed;

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(endpoint.port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        int connectResult = ::connect(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
        if (connectResult != 0) {
            const int error = detail::lastServiceSocketError();
            if (!detail::isWouldBlock(error)) return TransportResult::Disconnected;
            if (!detail::waitForSocket(socket, true, deadline)) return TransportResult::Timeout;
            int socketError = 0;
#if defined(_WIN32)
            int socketErrorBytes = sizeof(socketError);
#else
            socklen_t socketErrorBytes = sizeof(socketError);
#endif
            if (getsockopt(socket, SOL_SOCKET, SO_ERROR,
                    reinterpret_cast<char*>(&socketError), &socketErrorBytes) != 0 || socketError != 0)
                return TransportResult::Disconnected;
        }

        bool disconnected = false;
        if (!detail::transferAll(socket, const_cast<uint8_t*>(request), requestBytes,
                true, deadline, disconnected))
            return disconnected ? TransportResult::Disconnected : TransportResult::Timeout;
        if (!detail::transferAll(socket, response, kResponseHeaderBytes,
                false, deadline, disconnected))
            return disconnected ? TransportResult::Disconnected : TransportResult::Timeout;

        const uint32_t totalBytes = readU32(response + 16);
        if (totalBytes < kResponseHeaderBytes || totalBytes > kMaxResponseBytes ||
            totalBytes > responseCapacity) return TransportResult::Failed;
        const size_t bodyBytes = totalBytes - kResponseHeaderBytes;
        if (bodyBytes != 0 && !detail::transferAll(socket,
                response + kResponseHeaderBytes, bodyBytes, false,
                deadline, disconnected))
            return disconnected ? TransportResult::Disconnected : TransportResult::Timeout;
        *responseBytes = totalBytes;
        return TransportResult::Ok;
    }

    QemuCom2Endpoint m_endpoint{};
};

} // namespace system_service
} // namespace gxos
