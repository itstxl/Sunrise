#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <sunrise/realm/control_protocol.h>

namespace sunrise::realm::windows {

using SocketHandle = std::uintptr_t;
inline constexpr SocketHandle kInvalidSocketHandle = ~SocketHandle{};

enum class ReceiveStatus {
    complete,
    timeout,
    closed,
    invalid,
    failed,
};

[[nodiscard]] bool initialize_sockets() noexcept;
void shutdown_sockets() noexcept;
void close_socket(SocketHandle& socket) noexcept;

[[nodiscard]] SocketHandle listen_tcp(std::uint32_t hostOrderIpv4, std::uint16_t port) noexcept;
[[nodiscard]] SocketHandle accept_tcp(SocketHandle listener,
                                      std::uint32_t timeoutMilliseconds) noexcept;
[[nodiscard]] SocketHandle connect_tcp(std::uint32_t hostOrderIpv4,
                                       std::uint16_t port,
                                       std::uint32_t timeoutMilliseconds) noexcept;

[[nodiscard]] bool send_frame(SocketHandle socket,
                              const control::Frame& frame,
                              std::span<const std::byte> key) noexcept;
[[nodiscard]] ReceiveStatus receive_frame(SocketHandle socket,
                                          control::Frame& output,
                                          std::span<const std::byte> key,
                                          std::uint32_t timeoutMilliseconds) noexcept;

} // namespace sunrise::realm::windows
