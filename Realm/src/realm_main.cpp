#include <Windows.h>

#include <WS2tcpip.h>
#include <WinSock2.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <optional>
#include <ranges>
#include <string_view>
#include <sunrise/realm/control_protocol.h>
#include <vector>

#include "windows/console_runtime.h"
#include "windows/control_transport.h"
#include "windows/frame_authentication.h"

namespace {

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

constexpr std::string_view kVersion = "Sunrise Realm 0.1.0-dev";
constexpr std::uint16_t kDefaultControlPort = 31090;
constexpr std::uint32_t kHeartbeatIntervalMilliseconds = 5000;
constexpr std::uint32_t kLeaseTimeoutMilliseconds = 15000;
struct Configuration {
    std::uint32_t bindAddress{0x7F000001U};
    std::uint16_t controlPort{kDefaultControlPort};
    std::optional<sunrise::realm::ActivitySpec> startupActivity{};
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

[[nodiscard]] bool parse_port(const wchar_t* text, std::uint16_t& output) noexcept {
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

[[nodiscard]] bool parse_hash(const wchar_t* text, std::uint32_t& output) noexcept {
    if (text == nullptr || *text == L'\0') {
        return false;
    }
    wchar_t* end{};
    const unsigned long long value = std::wcstoull(text, &end, 0);
    if (end == text || *end != L'\0' || value == 0
        || value > (std::numeric_limits<std::uint32_t>::max)()) {
        return false;
    }
    output = static_cast<std::uint32_t>(value);
    return true;
}

[[nodiscard]] bool
parse_configuration(int count, wchar_t** values, Configuration& output) noexcept {
    if (const auto* bind = argument_value(count, values, L"--control-bind");
        bind != nullptr && !parse_ipv4(bind, output.bindAddress)) {
        return false;
    }
    if (const auto* port = argument_value(count, values, L"--control-port");
        port != nullptr && !parse_port(port, output.controlPort)) {
        return false;
    }
    const auto* activity = argument_value(count, values, L"--activity-definition");
    const auto* scenario = argument_value(count, values, L"--scenario-tag");
    const auto* mode = argument_value(count, values, L"--mode-definition");
    const bool anyActivityArgument = activity != nullptr || scenario != nullptr || mode != nullptr;
    const bool allActivityArguments = activity != nullptr && scenario != nullptr && mode != nullptr;
    if (anyActivityArgument && !allActivityArguments) {
        return false;
    }
    if (allActivityArguments) {
        sunrise::realm::ActivitySpec spec{};
        if (!parse_hash(activity, spec.activityDefinitionHash)
            || !parse_hash(scenario, spec.scenarioTagHash)
            || !parse_hash(mode, spec.modeDefinitionHash)) {
            return false;
        }
        spec.maximumPlayers = 2;
        spec.scoreLimit = 5;
        spec.timeLimitSeconds = 300;
        spec.respawnDelayMilliseconds = 3000;
        output.startupActivity = spec;
    }
    return true;
}

[[nodiscard]] bool nonzero(const sunrise::realm::BuildFingerprint& fingerprint) noexcept {
    return std::ranges::any_of(fingerprint.bytes,
                               [](std::byte value) { return value != std::byte{}; });
}

[[nodiscard]] HostRegistrationStatus validate(const HostRegistration& registration) noexcept {
    if (registration.realmId != kOfficialRealmId) {
        return HostRegistrationStatus::invalidRealm;
    }
    if (!nonzero(registration.build)) {
        return HostRegistrationStatus::incompatibleBuild;
    }
    if (registration.udpBasePort == 0 || registration.udpPortCount == 0
        || registration.activityCapacity == 0 || registration.playerCapacity == 0
        || registration.playerCapacity < registration.activityCapacity) {
        return HostRegistrationStatus::invalidRegistration;
    }
    const std::uint32_t lastPort =
        static_cast<std::uint32_t>(registration.udpBasePort) + registration.udpPortCount - 1;
    return lastPort <= 65535 ? HostRegistrationStatus::accepted
                             : HostRegistrationStatus::invalidRegistration;
}

void log_registration(const HostRegistration& registration,
                      HostRegistrationStatus status) noexcept {
    std::array<char, 256> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=host_registration result=%s udp_base=%u udp_count=%u "
                                      "activity_capacity=%u player_capacity=%u",
                                      status == HostRegistrationStatus::accepted ? "ok" : "reject",
                                      registration.udpBasePort,
                                      registration.udpPortCount,
                                      registration.activityCapacity,
                                      registration.playerCapacity);
    if (written > 0) {
        sunrise::realm::windows::write_line(line.data());
    }
}

void serve_host(SocketHandle host,
                std::span<const std::byte> key,
                const Configuration& configuration) noexcept {
    Frame frame{};
    if (sunrise::realm::windows::receive_frame(host, frame, key, 5000) != ReceiveStatus::complete
        || frame.type != MessageType::hostRegister || frame.sequence != 1) {
        sunrise::realm::windows::write_line("ev=host_registration result=invalid_frame");
        return;
    }
    HostRegistration registration{};
    if (!sunrise::realm::control::decode_host_registration(frame.payload, registration)) {
        sunrise::realm::windows::write_line("ev=host_registration result=invalid_payload");
        return;
    }
    const HostRegistrationStatus status = validate(registration);
    log_registration(registration, status);

    HostRegistrationResult result{
        kOfficialRealmId, status, kHeartbeatIntervalMilliseconds, kLeaseTimeoutMilliseconds};
    Frame response{MessageType::hostRegisterResult, frame.sequence};
    if (!sunrise::realm::control::encode_host_registration_result(result, response.payload)
        || !sunrise::realm::windows::send_frame(host, response, key)
        || status != HostRegistrationStatus::accepted) {
        return;
    }

    std::uint64_t outboundSequence = response.sequence;
    std::optional<Identifier> pendingAllocation{};
    if (configuration.startupActivity.has_value()) {
        sunrise::realm::ActivitySpec spec = *configuration.startupActivity;
        if (!sunrise::realm::windows::random_identifier(spec.activityId)) {
            sunrise::realm::windows::write_line("ev=activity_allocation result=identity_fail");
            return;
        }
        Frame allocation{MessageType::allocateActivity, ++outboundSequence};
        if (!sunrise::realm::control::encode_activity_spec(spec, allocation.payload)
            || !sunrise::realm::windows::send_frame(host, allocation, key)) {
            sunrise::realm::windows::write_line("ev=activity_allocation result=send_fail");
            return;
        }
        pendingAllocation = spec.activityId;
    }

    std::uint64_t lastSequence = frame.sequence;
    while (!sunrise::realm::windows::shutdown_requested()) {
        const auto receive =
            sunrise::realm::windows::receive_frame(host, frame, key, kLeaseTimeoutMilliseconds);
        if (receive == ReceiveStatus::timeout) {
            sunrise::realm::windows::write_line("ev=host_lease result=expired");
            return;
        }
        if (receive == ReceiveStatus::closed) {
            sunrise::realm::windows::write_line("ev=host_lease result=disconnected");
            return;
        }
        if (receive != ReceiveStatus::complete || frame.sequence <= lastSequence) {
            sunrise::realm::windows::write_line("ev=host_lease result=invalid_heartbeat");
            return;
        }
        lastSequence = frame.sequence;
        if (frame.type == MessageType::allocateActivityResult && pendingAllocation.has_value()) {
            sunrise::realm::ActivityAllocationResult allocation{};
            if (!sunrise::realm::control::decode_activity_allocation_result(frame.payload,
                                                                            allocation)
                || allocation.activityId != *pendingAllocation) {
                sunrise::realm::windows::write_line(
                    "ev=activity_allocation result=invalid_response");
                return;
            }
            sunrise::realm::windows::write_line(
                allocation.status == sunrise::realm::ActivityAllocationStatus::accepted
                    ? "ev=activity_allocation result=ok"
                    : "ev=activity_allocation result=reject");
            pendingAllocation.reset();
            continue;
        }
        if (frame.type != MessageType::hostHeartbeat) {
            sunrise::realm::windows::write_line("ev=host_lease result=unexpected_message");
            return;
        }
        HostHeartbeat heartbeat{};
        if (!sunrise::realm::control::decode_host_heartbeat(frame.payload, heartbeat)
            || heartbeat.hostId != registration.hostId) {
            sunrise::realm::windows::write_line("ev=host_lease result=invalid_identity");
            return;
        }
        sunrise::realm::windows::write_line("ev=host_heartbeat result=ok");
    }
}

} // namespace

int wmain(int count, wchar_t** values) {
    using sunrise::realm::windows::write_line;
    if (has_argument(count, values, L"--help")) {
        write_line("Usage: sunrise-realm [--control-bind IPv4] [--control-port PORT]");
        write_line(
            "       [--activity-definition HASH --scenario-tag HASH --mode-definition HASH]");
        write_line("Set SUNRISE_REALM_HOST_TOKEN to at least 32 UTF-8 bytes.");
        return 0;
    }
    if (has_argument(count, values, L"--version")) {
        write_line(kVersion);
        return 0;
    }
    Configuration configuration{};
    if (!parse_configuration(count, values, configuration)) {
        write_line("ev=realm stage=configuration result=fail");
        return 1;
    }
    std::vector<std::byte> key;
    if (!sunrise::realm::windows::load_host_token(key)) {
        write_line("ev=realm stage=host_token result=fail reason=missing_or_short");
        return 1;
    }
    if (!sunrise::realm::windows::initialize_console()
        || !sunrise::realm::windows::initialize_sockets()) {
        write_line("ev=realm stage=platform result=fail");
        return 1;
    }
    SocketHandle listener =
        sunrise::realm::windows::listen_tcp(configuration.bindAddress, configuration.controlPort);
    if (listener == sunrise::realm::windows::kInvalidSocketHandle) {
        write_line("ev=realm stage=control_listener result=fail");
        sunrise::realm::windows::shutdown_sockets();
        return 1;
    }
    write_line("ev=realm stage=control_listener result=ok");
    while (!sunrise::realm::windows::shutdown_requested()) {
        SocketHandle host = sunrise::realm::windows::accept_tcp(listener, 500);
        if (host == sunrise::realm::windows::kInvalidSocketHandle) {
            continue;
        }
        serve_host(host, key, configuration);
        sunrise::realm::windows::close_socket(host);
    }
    sunrise::realm::windows::close_socket(listener);
    sunrise::realm::windows::shutdown_sockets();
    std::fill(key.begin(), key.end(), std::byte{});
    write_line("ev=realm stage=shutdown result=ok");
    return 0;
}
