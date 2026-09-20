#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "contracts.h"

namespace sunrise::realm::control {

inline constexpr std::uint32_t kMagic = 0x53524C4DU; // SRLM
inline constexpr std::uint16_t kVersion = 1;
inline constexpr std::size_t kAuthenticationTagSize = 32;
inline constexpr std::size_t kHeaderSize = 56;
inline constexpr std::size_t kMaximumPayloadSize = 64 * 1024;

enum class MessageType : std::uint16_t {
    hostRegister = 1,
    hostRegisterResult = 2,
    hostHeartbeat = 3,
    allocateActivity = 4,
    allocateActivityResult = 5,
    admitPlayer = 6,
    removePlayer = 7,
    startActivity = 8,
    activityResult = 9,
    shutdown = 10,
};

enum class DecodeStatus {
    complete,
    incomplete,
    invalidMagic,
    unsupportedVersion,
    invalidType,
    payloadTooLarge,
};

struct Frame {
    MessageType type{MessageType::hostRegister};
    std::uint64_t sequence{};
    std::uint32_t flags{};
    std::array<std::byte, kAuthenticationTagSize> authenticationTag{};
    std::vector<std::byte> payload{};

    [[nodiscard]] bool operator==(const Frame&) const noexcept = default;
};

struct DecodeResult {
    DecodeStatus status{DecodeStatus::incomplete};
    std::size_t consumed{};
};

/** Serializes one authenticated control frame in network byte order. */
[[nodiscard]] bool encode_frame(const Frame& frame, std::vector<std::byte>& output) noexcept;

/** Decodes at most one complete frame without consuming partial input. */
[[nodiscard]] DecodeResult decode_frame(std::span<const std::byte> input, Frame& output) noexcept;

[[nodiscard]] bool encode_host_registration(const HostRegistration& registration,
                                            std::vector<std::byte>& output) noexcept;
[[nodiscard]] bool decode_host_registration(std::span<const std::byte> input,
                                            HostRegistration& output) noexcept;

[[nodiscard]] bool encode_host_registration_result(const HostRegistrationResult& result,
                                                   std::vector<std::byte>& output) noexcept;
[[nodiscard]] bool decode_host_registration_result(std::span<const std::byte> input,
                                                   HostRegistrationResult& output) noexcept;

[[nodiscard]] bool encode_host_heartbeat(const HostHeartbeat& heartbeat,
                                         std::vector<std::byte>& output) noexcept;
[[nodiscard]] bool decode_host_heartbeat(std::span<const std::byte> input,
                                         HostHeartbeat& output) noexcept;

[[nodiscard]] bool encode_activity_spec(const ActivitySpec& activity,
                                        std::vector<std::byte>& output) noexcept;
[[nodiscard]] bool decode_activity_spec(std::span<const std::byte> input,
                                        ActivitySpec& output) noexcept;

[[nodiscard]] bool encode_activity_allocation_result(const ActivityAllocationResult& result,
                                                     std::vector<std::byte>& output) noexcept;
[[nodiscard]] bool decode_activity_allocation_result(std::span<const std::byte> input,
                                                     ActivityAllocationResult& output) noexcept;

} // namespace sunrise::realm::control
