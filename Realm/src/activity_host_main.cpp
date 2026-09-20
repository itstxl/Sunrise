#include <Windows.h>

#include <WS2tcpip.h>
#include <WinSock2.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <ranges>
#include <string_view>
#include <sunrise/realm/activity_host_state.h>
#include <sunrise/realm/control_protocol.h>
#include <vector>

#include "windows/console_runtime.h"
#include "windows/control_transport.h"
#include "windows/frame_authentication.h"

namespace {

using sunrise::realm::BuildFingerprint;
using sunrise::realm::HostHeartbeat;
using sunrise::realm::HostRegistration;
using sunrise::realm::HostRegistrationResult;
using sunrise::realm::HostRegistrationStatus;
using sunrise::realm::Identifier;
using sunrise::realm::kOfficialRealmId;
using sunrise::realm::control::Frame;
using sunrise::realm::control::MessageType;
using sunrise::realm::windows::ReceiveStatus;
using sunrise::realm::windows::SocketHandle;

constexpr std::string_view kVersion = "Sunrise Activity Host 0.1.0-dev";
struct Configuration {
    std::uint32_t realmAddress{0x7F000001U};
    std::uint16_t controlPort{31090};
    std::uint32_t advertisedAddress{0x7F000001U};
    std::uint16_t udpBasePort{30976};
    std::uint16_t udpPortCount{16};
    std::uint16_t activityCapacity{1};
    std::uint16_t playerCapacity{8};
    BuildFingerprint build{};
};

[[nodiscard]] const wchar_t*
argument_value(int count, wchar_t** values, std::wstring_view name) noexcept {
    for (int index = 1; index + 1 < count; ++index) {
        if (values[index] != nullptr && std::wstring_view(values[index]) == name) {
            return values[index + 1];
        }
    }
    return nullptr;
}

[[nodiscard]] bool has_argument(int count, wchar_t** values, std::wstring_view expected) noexcept {
    for (int index = 1; index < count; ++index) {
        if (values[index] != nullptr && std::wstring_view(values[index]) == expected) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool parse_u16(const wchar_t* text, std::uint16_t& output) noexcept {
    if (text == nullptr || *text == L'\0') {
        return false;
    }
    wchar_t* end{};
    const unsigned long value = std::wcstoul(text, &end, 10);
    if (end == text || *end != L'\0' || value == 0 || value > 65535) {
        return false;
    }
    output = static_cast<std::uint16_t>(value);
    return true;
}

[[nodiscard]] bool parse_ipv4(const wchar_t* text, std::uint32_t& output) noexcept {
    if (text == nullptr) {
        return false;
    }
    IN_ADDR address{};
    if (InetPtonW(AF_INET, text, &address) != 1) {
        return false;
    }
    output = ntohl(address.S_un.S_addr);
    return true;
}

[[nodiscard]] int hex_digit(wchar_t value) noexcept {
    if (value >= L'0' && value <= L'9') {
        return value - L'0';
    }
    if (value >= L'a' && value <= L'f') {
        return value - L'a' + 10;
    }
    if (value >= L'A' && value <= L'F') {
        return value - L'A' + 10;
    }
    return -1;
}

[[nodiscard]] bool parse_fingerprint(const wchar_t* text, BuildFingerprint& output) noexcept {
    if (text == nullptr || std::wcslen(text) != output.bytes.size() * 2) {
        return false;
    }
    for (std::size_t index = 0; index < output.bytes.size(); ++index) {
        const int high = hex_digit(text[index * 2]);
        const int low = hex_digit(text[index * 2 + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        output.bytes[index] = static_cast<std::byte>((high << 4) | low);
    }
    return std::ranges::any_of(output.bytes, [](std::byte value) { return value != std::byte{}; });
}

[[nodiscard]] bool
parse_configuration(int count, wchar_t** values, Configuration& output) noexcept {
    const auto* catalog = argument_value(count, values, L"--content-catalog");
    const auto* explicitFingerprint = argument_value(count, values, L"--build-fingerprint");
    if ((catalog == nullptr) == (explicitFingerprint == nullptr)) {
        return false;
    }
    if (catalog != nullptr && !sunrise::realm::windows::sha256_file(catalog, output.build)) {
        return false;
    }
    if (explicitFingerprint != nullptr && !parse_fingerprint(explicitFingerprint, output.build)) {
        return false;
    }
    if (const auto* value = argument_value(count, values, L"--realm-address");
        value != nullptr && !parse_ipv4(value, output.realmAddress)) {
        return false;
    }
    if (const auto* value = argument_value(count, values, L"--control-port");
        value != nullptr && !parse_u16(value, output.controlPort)) {
        return false;
    }
    if (const auto* value = argument_value(count, values, L"--advertise-address");
        value != nullptr && !parse_ipv4(value, output.advertisedAddress)) {
        return false;
    }
    if (const auto* value = argument_value(count, values, L"--udp-base-port");
        value != nullptr && !parse_u16(value, output.udpBasePort)) {
        return false;
    }
    if (const auto* value = argument_value(count, values, L"--udp-port-count");
        value != nullptr && !parse_u16(value, output.udpPortCount)) {
        return false;
    }
    if (const auto* value = argument_value(count, values, L"--activity-capacity");
        value != nullptr && !parse_u16(value, output.activityCapacity)) {
        return false;
    }
    if (const auto* value = argument_value(count, values, L"--player-capacity");
        value != nullptr && !parse_u16(value, output.playerCapacity)) {
        return false;
    }
    return output.playerCapacity >= output.activityCapacity
           && static_cast<std::uint32_t>(output.udpBasePort) + output.udpPortCount - 1 <= 65535;
}

[[nodiscard]] std::uint16_t
connected_players(const sunrise::realm::ActivityHostState& state) noexcept {
    const auto* match = state.ffa_match();
    if (match == nullptr) {
        return 0;
    }
    std::uint16_t count{};
    for (const auto& standing : match->standings()) {
        if (standing.occupied && standing.connected) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] bool handle_realm_request(const Frame& request,
                                        sunrise::realm::ActivityHostState& state,
                                        SocketHandle realm,
                                        std::span<const std::byte> key,
                                        std::uint64_t& outboundSequence) noexcept {
    if (request.type == MessageType::shutdown) {
        return false;
    }
    if (request.type != MessageType::allocateActivity) {
        sunrise::realm::windows::write_line(
            "ev=activity_host stage=control result=unknown_request");
        return false;
    }
    sunrise::realm::ActivitySpec spec{};
    if (!sunrise::realm::control::decode_activity_spec(request.payload, spec)) {
        sunrise::realm::windows::write_line(
            "ev=activity_host stage=allocation result=invalid_payload");
        return false;
    }
    const sunrise::realm::ActivityAllocationResult result = state.allocate(spec);
    Frame response{MessageType::allocateActivityResult, ++outboundSequence};
    if (!sunrise::realm::control::encode_activity_allocation_result(result, response.payload)
        || !sunrise::realm::windows::send_frame(realm, response, key)) {
        return false;
    }
    sunrise::realm::windows::write_line(
        result.status == sunrise::realm::ActivityAllocationStatus::accepted
            ? "ev=activity_host stage=allocation result=ok"
            : "ev=activity_host stage=allocation result=reject");
    return true;
}

} // namespace

int wmain(int count, wchar_t** values) {
    using sunrise::realm::windows::write_line;
    if (has_argument(count, values, L"--help")) {
        write_line("Usage: sunrise-activity-host --content-catalog PATH [options]");
        write_line("Usage: sunrise-activity-host --build-fingerprint 64_HEX_DIGITS [options]");
        write_line("Options: --realm-address IPv4 --control-port PORT --advertise-address IPv4");
        write_line("         --udp-base-port PORT --udp-port-count N --activity-capacity N");
        write_line("         --player-capacity N");
        write_line("Set SUNRISE_REALM_HOST_TOKEN to at least 32 UTF-8 bytes.");
        return 0;
    }
    if (has_argument(count, values, L"--version")) {
        write_line(kVersion);
        return 0;
    }
    Configuration configuration{};
    if (!parse_configuration(count, values, configuration)) {
        write_line("ev=activity_host stage=configuration result=fail");
        return 1;
    }
    std::vector<std::byte> key;
    if (!sunrise::realm::windows::load_host_token(key)) {
        write_line("ev=activity_host stage=host_token result=fail reason=missing_or_short");
        return 1;
    }
    if (!sunrise::realm::windows::initialize_console()
        || !sunrise::realm::windows::initialize_sockets()) {
        write_line("ev=activity_host stage=platform result=fail");
        return 1;
    }
    SocketHandle realm = sunrise::realm::windows::connect_tcp(
        configuration.realmAddress, configuration.controlPort, 5000);
    if (realm == sunrise::realm::windows::kInvalidSocketHandle) {
        write_line("ev=activity_host stage=realm_connect result=fail");
        sunrise::realm::windows::shutdown_sockets();
        return 1;
    }
    Identifier hostId{};
    if (!sunrise::realm::windows::random_identifier(hostId)) {
        write_line("ev=activity_host stage=host_identity result=fail");
        sunrise::realm::windows::close_socket(realm);
        sunrise::realm::windows::shutdown_sockets();
        return 1;
    }
    const HostRegistration registration{kOfficialRealmId,
                                        hostId,
                                        configuration.build,
                                        configuration.advertisedAddress,
                                        configuration.udpBasePort,
                                        configuration.udpPortCount,
                                        configuration.activityCapacity,
                                        configuration.playerCapacity};
    Frame frame{MessageType::hostRegister, 1};
    if (!sunrise::realm::control::encode_host_registration(registration, frame.payload)
        || !sunrise::realm::windows::send_frame(realm, frame, key)) {
        write_line("ev=activity_host stage=realm_registration result=send_fail");
        sunrise::realm::windows::close_socket(realm);
        sunrise::realm::windows::shutdown_sockets();
        return 1;
    }
    if (sunrise::realm::windows::receive_frame(realm, frame, key, 5000) != ReceiveStatus::complete
        || frame.type != MessageType::hostRegisterResult || frame.sequence != 1) {
        write_line("ev=activity_host stage=realm_registration result=invalid_response");
        sunrise::realm::windows::close_socket(realm);
        sunrise::realm::windows::shutdown_sockets();
        return 1;
    }
    HostRegistrationResult registrationResult{};
    if (!sunrise::realm::control::decode_host_registration_result(frame.payload, registrationResult)
        || registrationResult.realmId != kOfficialRealmId
        || registrationResult.status != HostRegistrationStatus::accepted
        || registrationResult.heartbeatIntervalMilliseconds == 0) {
        write_line("ev=activity_host stage=realm_registration result=rejected");
        sunrise::realm::windows::close_socket(realm);
        sunrise::realm::windows::shutdown_sockets();
        return 1;
    }
    write_line("ev=activity_host stage=realm_registration result=ok");
    std::uint64_t outboundSequence = 1;
    std::uint64_t inboundSequence = frame.sequence;
    const std::uint64_t started = GetTickCount64();
    std::uint64_t nextHeartbeat = started + registrationResult.heartbeatIntervalMilliseconds;
    sunrise::realm::ActivityHostState activityState;
    while (!sunrise::realm::windows::shutdown_requested()) {
        const auto receive = sunrise::realm::windows::receive_frame(realm, frame, key, 250);
        if (receive == ReceiveStatus::complete) {
            if (frame.sequence <= inboundSequence
                || !handle_realm_request(frame, activityState, realm, key, outboundSequence)) {
                write_line("ev=activity_host stage=control result=fail");
                sunrise::realm::windows::close_socket(realm);
                sunrise::realm::windows::shutdown_sockets();
                std::fill(key.begin(), key.end(), std::byte{});
                return 1;
            }
            inboundSequence = frame.sequence;
        } else if (receive != ReceiveStatus::timeout) {
            write_line("ev=activity_host stage=control result=disconnected");
            sunrise::realm::windows::close_socket(realm);
            sunrise::realm::windows::shutdown_sockets();
            std::fill(key.begin(), key.end(), std::byte{});
            return 1;
        }
        const std::uint64_t now = GetTickCount64();
        if (now < nextHeartbeat) {
            continue;
        }
        const HostHeartbeat heartbeat{
            hostId,
            now - started,
            static_cast<std::uint16_t>(activityState.activity() != nullptr),
            connected_players(activityState)};
        frame = {MessageType::hostHeartbeat, ++outboundSequence};
        if (!sunrise::realm::control::encode_host_heartbeat(heartbeat, frame.payload)
            || !sunrise::realm::windows::send_frame(realm, frame, key)) {
            write_line("ev=activity_host stage=heartbeat result=fail");
            sunrise::realm::windows::close_socket(realm);
            sunrise::realm::windows::shutdown_sockets();
            std::fill(key.begin(), key.end(), std::byte{});
            return 1;
        }
        write_line("ev=activity_host stage=heartbeat result=ok");
        nextHeartbeat = now + registrationResult.heartbeatIntervalMilliseconds;
    }
    sunrise::realm::windows::close_socket(realm);
    sunrise::realm::windows::shutdown_sockets();
    std::fill(key.begin(), key.end(), std::byte{});
    write_line("ev=activity_host stage=shutdown result=ok");
    return 0;
}
