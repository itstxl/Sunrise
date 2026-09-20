#include "control_transport.h"

#include <WS2tcpip.h>
#include <WinSock2.h>
#include <array>
#include <climits>
#include <limits>
#include <vector>

#include "frame_authentication.h"

namespace sunrise::realm::windows {
namespace {

[[nodiscard]] SOCKET native(SocketHandle socket) noexcept {
    return static_cast<SOCKET>(socket);
}

[[nodiscard]] bool
wait_for(SOCKET socket, bool readable, std::uint32_t timeoutMilliseconds) noexcept {
    fd_set set{};
    FD_ZERO(&set);
    FD_SET(socket, &set);
    timeval timeout{static_cast<long>(timeoutMilliseconds / 1000),
                    static_cast<long>((timeoutMilliseconds % 1000) * 1000)};
    const int result =
        select(0, readable ? &set : nullptr, readable ? nullptr : &set, nullptr, &timeout);
    return result > 0 && FD_ISSET(socket, &set);
}

[[nodiscard]] ReceiveStatus receive_exact(SOCKET socket,
                                          std::span<std::byte> output,
                                          std::uint32_t timeoutMilliseconds) noexcept {
    std::size_t offset{};
    while (offset < output.size()) {
        if (!wait_for(socket, true, timeoutMilliseconds)) {
            return WSAGetLastError() == 0 ? ReceiveStatus::timeout : ReceiveStatus::failed;
        }
        const std::size_t remaining = output.size() - offset;
        const int received =
            recv(socket,
                 reinterpret_cast<char*>(output.data() + offset),
                 static_cast<int>((std::min)(remaining, static_cast<std::size_t>(INT_MAX))),
                 0);
        if (received == 0) {
            return ReceiveStatus::closed;
        }
        if (received == SOCKET_ERROR) {
            return ReceiveStatus::failed;
        }
        offset += static_cast<std::size_t>(received);
    }
    return ReceiveStatus::complete;
}

} // namespace

bool initialize_sockets() noexcept {
    WSADATA data{};
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

void shutdown_sockets() noexcept {
    (void)WSACleanup();
}

void close_socket(SocketHandle& socket) noexcept {
    if (socket != kInvalidSocketHandle) {
        (void)closesocket(native(socket));
        socket = kInvalidSocketHandle;
    }
}

SocketHandle listen_tcp(std::uint32_t hostOrderIpv4, std::uint16_t port) noexcept {
    const SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        return kInvalidSocketHandle;
    }
    BOOL exclusive = TRUE;
    if (setsockopt(listener,
                   SOL_SOCKET,
                   SO_EXCLUSIVEADDRUSE,
                   reinterpret_cast<const char*>(&exclusive),
                   sizeof(exclusive))
        == SOCKET_ERROR) {
        (void)closesocket(listener);
        return kInvalidSocketHandle;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(hostOrderIpv4);
    if (bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR
        || listen(listener, SOMAXCONN) == SOCKET_ERROR) {
        (void)closesocket(listener);
        return kInvalidSocketHandle;
    }
    return static_cast<SocketHandle>(listener);
}

SocketHandle accept_tcp(SocketHandle listener, std::uint32_t timeoutMilliseconds) noexcept {
    const SOCKET value = native(listener);
    if (!wait_for(value, true, timeoutMilliseconds)) {
        return kInvalidSocketHandle;
    }
    const SOCKET accepted = accept(value, nullptr, nullptr);
    return accepted == INVALID_SOCKET ? kInvalidSocketHandle : static_cast<SocketHandle>(accepted);
}

SocketHandle connect_tcp(std::uint32_t hostOrderIpv4,
                         std::uint16_t port,
                         std::uint32_t timeoutMilliseconds) noexcept {
    const SOCKET connection = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (connection == INVALID_SOCKET) {
        return kInvalidSocketHandle;
    }
    u_long nonblocking = 1;
    if (ioctlsocket(connection, FIONBIO, &nonblocking) == SOCKET_ERROR) {
        (void)closesocket(connection);
        return kInvalidSocketHandle;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(hostOrderIpv4);
    const int result =
        connect(connection, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    if (result == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK
        && WSAGetLastError() != WSAEINPROGRESS) {
        (void)closesocket(connection);
        return kInvalidSocketHandle;
    }
    if (result == SOCKET_ERROR && !wait_for(connection, false, timeoutMilliseconds)) {
        (void)closesocket(connection);
        return kInvalidSocketHandle;
    }
    int error{};
    int errorSize = sizeof(error);
    if (getsockopt(connection, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &errorSize)
            == SOCKET_ERROR
        || error != 0) {
        (void)closesocket(connection);
        return kInvalidSocketHandle;
    }
    nonblocking = 0;
    if (ioctlsocket(connection, FIONBIO, &nonblocking) == SOCKET_ERROR) {
        (void)closesocket(connection);
        return kInvalidSocketHandle;
    }
    return static_cast<SocketHandle>(connection);
}

bool send_frame(SocketHandle socket,
                const control::Frame& source,
                std::span<const std::byte> key) noexcept {
    control::Frame frame{};
    std::vector<std::byte> wire;
    try {
        frame = source;
    } catch (...) {
        return false;
    }
    if (!sign_frame(frame, key) || !control::encode_frame(frame, wire)) {
        return false;
    }
    std::size_t offset{};
    while (offset < wire.size()) {
        const std::size_t remaining = wire.size() - offset;
        const int sent =
            send(native(socket),
                 reinterpret_cast<const char*>(wire.data() + offset),
                 static_cast<int>((std::min)(remaining, static_cast<std::size_t>(INT_MAX))),
                 0);
        if (sent <= 0) {
            return false;
        }
        offset += static_cast<std::size_t>(sent);
    }
    return true;
}

ReceiveStatus receive_frame(SocketHandle socket,
                            control::Frame& output,
                            std::span<const std::byte> key,
                            std::uint32_t timeoutMilliseconds) noexcept {
    std::array<std::byte, control::kHeaderSize> header{};
    auto status = receive_exact(native(socket), header, timeoutMilliseconds);
    if (status != ReceiveStatus::complete) {
        return status;
    }
    control::Frame decoded{};
    auto result = control::decode_frame(header, decoded);
    if (result.status != control::DecodeStatus::incomplete) {
        return ReceiveStatus::invalid;
    }
    const std::size_t sizeOffset = 12;
    const std::uint32_t payloadSize =
        (static_cast<std::uint32_t>(std::to_integer<unsigned char>(header[sizeOffset])) << 24)
        | (static_cast<std::uint32_t>(std::to_integer<unsigned char>(header[sizeOffset + 1])) << 16)
        | (static_cast<std::uint32_t>(std::to_integer<unsigned char>(header[sizeOffset + 2])) << 8)
        | static_cast<std::uint32_t>(std::to_integer<unsigned char>(header[sizeOffset + 3]));
    if (payloadSize > control::kMaximumPayloadSize) {
        return ReceiveStatus::invalid;
    }
    std::vector<std::byte> wire;
    try {
        wire.assign(header.begin(), header.end());
        wire.resize(control::kHeaderSize + payloadSize);
    } catch (...) {
        return ReceiveStatus::failed;
    }
    if (payloadSize != 0) {
        status = receive_exact(native(socket),
                               std::span<std::byte>(wire).subspan(control::kHeaderSize),
                               timeoutMilliseconds);
        if (status != ReceiveStatus::complete) {
            return status;
        }
    }
    result = control::decode_frame(wire, decoded);
    if (result.status != control::DecodeStatus::complete || !verify_frame(decoded, key)) {
        return ReceiveStatus::invalid;
    }
    output = std::move(decoded);
    return ReceiveStatus::complete;
}

} // namespace sunrise::realm::windows
