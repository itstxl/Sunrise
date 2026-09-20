#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <sunrise/realm/control_protocol.h>
#include <vector>

namespace {

using sunrise::realm::ActivityAllocationResult;
using sunrise::realm::ActivityAllocationStatus;
using sunrise::realm::ActivitySpec;
using sunrise::realm::HostHeartbeat;
using sunrise::realm::HostRegistration;
using sunrise::realm::HostRegistrationResult;
using sunrise::realm::HostRegistrationStatus;
using sunrise::realm::control::DecodeStatus;
using sunrise::realm::control::Frame;
using sunrise::realm::control::MessageType;

int g_failures{};

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

template <std::size_t Size> void fill(std::array<std::byte, Size>& bytes, unsigned char seed) {
    for (std::size_t index = 0; index < Size; ++index) {
        bytes[index] = static_cast<std::byte>(seed + static_cast<unsigned char>(index));
    }
}

void frame_round_trip() {
    Frame source{};
    source.type = MessageType::allocateActivity;
    source.sequence = 0x0102030405060708ULL;
    source.flags = 0x10203040U;
    fill(source.authenticationTag, 0x40);
    source.payload = {std::byte{0x01}, std::byte{0x7F}, std::byte{0xFF}};

    std::vector<std::byte> wire;
    expect(sunrise::realm::control::encode_frame(source, wire), "frame encodes");
    Frame decoded{};
    const auto result = sunrise::realm::control::decode_frame(wire, decoded);
    expect(result.status == DecodeStatus::complete, "frame decodes");
    expect(result.consumed == wire.size(), "frame reports consumed bytes");
    expect(decoded == source, "frame round trips");

    Frame partial{};
    const auto partialResult = sunrise::realm::control::decode_frame(
        std::span<const std::byte>(wire).first(wire.size() - 1), partial);
    expect(partialResult.status == DecodeStatus::incomplete, "partial frame is retained");
    expect(partialResult.consumed == 0, "partial frame consumes nothing");

    auto invalid = wire;
    invalid[0] = std::byte{0};
    expect(sunrise::realm::control::decode_frame(invalid, partial).status
               == DecodeStatus::invalidMagic,
           "invalid magic is rejected");
}

void registration_round_trip() {
    HostRegistration source{};
    fill(source.realmId.bytes, 0x10);
    fill(source.hostId.bytes, 0x20);
    fill(source.build.bytes, 0x30);
    source.ipv4Address = 0x7F000001U;
    source.udpBasePort = 30976;
    source.udpPortCount = 16;
    source.activityCapacity = 4;
    source.playerCapacity = 24;

    std::vector<std::byte> wire;
    expect(sunrise::realm::control::encode_host_registration(source, wire), "registration encodes");
    HostRegistration decoded{};
    expect(sunrise::realm::control::decode_host_registration(wire, decoded),
           "registration decodes");
    expect(decoded == source, "registration round trips");
    wire.push_back(std::byte{});
    expect(!sunrise::realm::control::decode_host_registration(wire, decoded),
           "registration rejects trailing bytes");
}

void registration_result_round_trip() {
    HostRegistrationResult source{};
    fill(source.realmId.bytes, 0x41);
    source.status = HostRegistrationStatus::accepted;
    source.heartbeatIntervalMilliseconds = 5000;
    source.leaseTimeoutMilliseconds = 15000;

    std::vector<std::byte> wire;
    expect(sunrise::realm::control::encode_host_registration_result(source, wire),
           "registration result encodes");
    HostRegistrationResult decoded{};
    expect(sunrise::realm::control::decode_host_registration_result(wire, decoded),
           "registration result decodes");
    expect(decoded == source, "registration result round trips");
}

void heartbeat_round_trip() {
    HostHeartbeat source{};
    fill(source.hostId.bytes, 0x50);
    source.monotonicMilliseconds = 123456789;
    source.activeActivities = 2;
    source.connectedPlayers = 11;

    std::vector<std::byte> wire;
    expect(sunrise::realm::control::encode_host_heartbeat(source, wire), "heartbeat encodes");
    HostHeartbeat decoded{};
    expect(sunrise::realm::control::decode_host_heartbeat(wire, decoded), "heartbeat decodes");
    expect(decoded == source, "heartbeat round trips");
}

void activity_round_trip() {
    ActivitySpec source{};
    fill(source.activityId.bytes, 0x70);
    source.activityDefinitionHash = 0x12345678U;
    source.scenarioTagHash = 0x80B60015U;
    source.modeDefinitionHash = 0x813206E7U;
    source.maximumPlayers = 2;
    source.scoreLimit = 5;
    source.timeLimitSeconds = 300;
    source.respawnDelayMilliseconds = 3000;

    std::vector<std::byte> wire;
    expect(sunrise::realm::control::encode_activity_spec(source, wire), "activity encodes");
    ActivitySpec decoded{};
    expect(sunrise::realm::control::decode_activity_spec(wire, decoded), "activity decodes");
    expect(decoded == source, "activity round trips");

    const ActivityAllocationResult allocation{source.activityId,
                                              ActivityAllocationStatus::accepted};
    expect(sunrise::realm::control::encode_activity_allocation_result(allocation, wire),
           "allocation result encodes");
    ActivityAllocationResult decodedAllocation{};
    expect(sunrise::realm::control::decode_activity_allocation_result(wire, decodedAllocation),
           "allocation result decodes");
    expect(decodedAllocation == allocation, "allocation result round trips");
}

} // namespace

int main() {
    frame_round_trip();
    registration_round_trip();
    registration_result_round_trip();
    heartbeat_round_trip();
    activity_round_trip();
    if (g_failures != 0) {
        std::cerr << g_failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "sunrise realm protocol tests passed\n";
    return 0;
}
