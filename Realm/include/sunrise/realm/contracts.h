#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace sunrise::realm {

/** Stable opaque identifier carried across every realm service boundary. */
struct Identifier {
    std::array<std::byte, 16> bytes{};

    [[nodiscard]] constexpr bool operator==(const Identifier&) const noexcept = default;
};

/** Stable namespace for the initial official realm; retained even while only one realm exists. */
inline constexpr Identifier kOfficialRealmId{{std::byte{0x53},
                                              std::byte{0x75},
                                              std::byte{0x6E},
                                              std::byte{0x72},
                                              std::byte{0x69},
                                              std::byte{0x73},
                                              std::byte{0x65},
                                              std::byte{0x52},
                                              std::byte{0x65},
                                              std::byte{0x61},
                                              std::byte{0x6C},
                                              std::byte{0x6D},
                                              std::byte{0x30},
                                              std::byte{0x30},
                                              std::byte{0x30},
                                              std::byte{0x31}}};

/** SHA-256 fingerprint of the supported executable and generated content catalogue. */
struct BuildFingerprint {
    std::array<std::byte, 32> bytes{};

    [[nodiscard]] constexpr bool operator==(const BuildFingerprint&) const noexcept = default;
};

/** Network capacity and content identity advertised by one activity host. */
struct HostRegistration {
    Identifier realmId{};
    Identifier hostId{};
    BuildFingerprint build{};
    std::uint32_t ipv4Address{};
    std::uint16_t udpBasePort{};
    std::uint16_t udpPortCount{};
    std::uint16_t activityCapacity{};
    std::uint16_t playerCapacity{};

    [[nodiscard]] constexpr bool operator==(const HostRegistration&) const noexcept = default;
};

enum class HostRegistrationStatus : std::uint16_t {
    accepted = 0,
    invalidRealm = 1,
    invalidRegistration = 2,
    incompatibleBuild = 3,
    capacityUnavailable = 4,
};

/** Realm decision and lease timings returned after host registration. */
struct HostRegistrationResult {
    Identifier realmId{};
    HostRegistrationStatus status{HostRegistrationStatus::invalidRegistration};
    std::uint32_t heartbeatIntervalMilliseconds{};
    std::uint32_t leaseTimeoutMilliseconds{};

    [[nodiscard]] constexpr bool operator==(const HostRegistrationResult&) const noexcept = default;
};

/** Liveness and load report sent while a host lease remains active. */
struct HostHeartbeat {
    Identifier hostId{};
    std::uint64_t monotonicMilliseconds{};
    std::uint16_t activeActivities{};
    std::uint16_t connectedPlayers{};

    [[nodiscard]] constexpr bool operator==(const HostHeartbeat&) const noexcept = default;
};

/** Activity chosen by an invitation, browser entry, or native Director request. */
struct ActivitySpec {
    Identifier activityId{};
    std::uint32_t activityDefinitionHash{};
    std::uint32_t scenarioTagHash{};
    std::uint32_t modeDefinitionHash{};
    std::uint16_t maximumPlayers{};
    std::uint16_t scoreLimit{};
    std::uint32_t timeLimitSeconds{};
    std::uint32_t respawnDelayMilliseconds{};

    [[nodiscard]] constexpr bool operator==(const ActivitySpec&) const noexcept = default;
};

enum class ActivityAllocationStatus : std::uint16_t {
    accepted = 0,
    invalidSpec = 1,
    capacityUnavailable = 2,
    duplicate = 3,
};

/** Host response to one realm allocation request. */
struct ActivityAllocationResult {
    Identifier activityId{};
    ActivityAllocationStatus status{ActivityAllocationStatus::invalidSpec};

    [[nodiscard]] constexpr bool
    operator==(const ActivityAllocationResult&) const noexcept = default;
};

} // namespace sunrise::realm
