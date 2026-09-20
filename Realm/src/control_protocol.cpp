#include <algorithm>
#include <limits>
#include <sunrise/realm/control_protocol.h>

namespace sunrise::realm::control {
namespace {

template <typename T> void append_integer(std::vector<std::byte>& output, T value) {
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        const auto shift = static_cast<unsigned>((sizeof(T) - index - 1) * 8);
        output.push_back(static_cast<std::byte>((value >> shift) & static_cast<T>(0xFF)));
    }
}

template <typename T>
[[nodiscard]] bool
read_integer(std::span<const std::byte> input, std::size_t& offset, T& output) noexcept {
    if (input.size() - std::min(input.size(), offset) < sizeof(T)) {
        return false;
    }
    T value{};
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        value = static_cast<T>((value << 8) | std::to_integer<unsigned char>(input[offset++]));
    }
    output = value;
    return true;
}

template <std::size_t Size>
void append_bytes(std::vector<std::byte>& output, const std::array<std::byte, Size>& value) {
    output.insert(output.end(), value.begin(), value.end());
}

template <std::size_t Size>
[[nodiscard]] bool read_bytes(std::span<const std::byte> input,
                              std::size_t& offset,
                              std::array<std::byte, Size>& output) noexcept {
    if (input.size() - std::min(input.size(), offset) < Size) {
        return false;
    }
    std::copy_n(input.begin() + static_cast<std::ptrdiff_t>(offset), Size, output.begin());
    offset += Size;
    return true;
}

[[nodiscard]] bool valid_type(MessageType type) noexcept {
    const auto raw = static_cast<std::uint16_t>(type);
    return raw >= static_cast<std::uint16_t>(MessageType::hostRegister)
           && raw <= static_cast<std::uint16_t>(MessageType::shutdown);
}

} // namespace

bool encode_frame(const Frame& frame, std::vector<std::byte>& output) noexcept {
    if (!valid_type(frame.type) || frame.payload.size() > kMaximumPayloadSize
        || frame.payload.size() > (std::numeric_limits<std::uint32_t>::max)()) {
        return false;
    }
    try {
        output.clear();
        output.reserve(kHeaderSize + frame.payload.size());
        append_integer(output, kMagic);
        append_integer(output, kVersion);
        append_integer(output, static_cast<std::uint16_t>(frame.type));
        append_integer(output, frame.flags);
        append_integer(output, static_cast<std::uint32_t>(frame.payload.size()));
        append_integer(output, frame.sequence);
        append_bytes(output, frame.authenticationTag);
        output.insert(output.end(), frame.payload.begin(), frame.payload.end());
        return true;
    } catch (...) {
        output.clear();
        return false;
    }
}

DecodeResult decode_frame(std::span<const std::byte> input, Frame& output) noexcept {
    if (input.size() < kHeaderSize) {
        return {DecodeStatus::incomplete, 0};
    }
    std::size_t offset{};
    std::uint32_t magic{};
    std::uint16_t version{};
    std::uint16_t rawType{};
    std::uint32_t flags{};
    std::uint32_t payloadSize{};
    std::uint64_t sequence{};
    std::array<std::byte, kAuthenticationTagSize> authenticationTag{};
    if (!read_integer(input, offset, magic) || !read_integer(input, offset, version)
        || !read_integer(input, offset, rawType) || !read_integer(input, offset, flags)
        || !read_integer(input, offset, payloadSize) || !read_integer(input, offset, sequence)
        || !read_bytes(input, offset, authenticationTag)) {
        return {DecodeStatus::incomplete, 0};
    }
    if (magic != kMagic) {
        return {DecodeStatus::invalidMagic, 0};
    }
    if (version != kVersion) {
        return {DecodeStatus::unsupportedVersion, 0};
    }
    const auto type = static_cast<MessageType>(rawType);
    if (!valid_type(type)) {
        return {DecodeStatus::invalidType, 0};
    }
    if (payloadSize > kMaximumPayloadSize) {
        return {DecodeStatus::payloadTooLarge, 0};
    }
    const std::size_t frameSize = kHeaderSize + payloadSize;
    if (input.size() < frameSize) {
        return {DecodeStatus::incomplete, 0};
    }
    try {
        Frame decoded{};
        decoded.type = type;
        decoded.flags = flags;
        decoded.sequence = sequence;
        decoded.authenticationTag = authenticationTag;
        decoded.payload.assign(input.begin() + static_cast<std::ptrdiff_t>(kHeaderSize),
                               input.begin() + static_cast<std::ptrdiff_t>(frameSize));
        output = std::move(decoded);
        return {DecodeStatus::complete, frameSize};
    } catch (...) {
        return {DecodeStatus::payloadTooLarge, 0};
    }
}

bool encode_host_registration(const HostRegistration& registration,
                              std::vector<std::byte>& output) noexcept {
    try {
        output.clear();
        output.reserve(76);
        append_bytes(output, registration.realmId.bytes);
        append_bytes(output, registration.hostId.bytes);
        append_bytes(output, registration.build.bytes);
        append_integer(output, registration.ipv4Address);
        append_integer(output, registration.udpBasePort);
        append_integer(output, registration.udpPortCount);
        append_integer(output, registration.activityCapacity);
        append_integer(output, registration.playerCapacity);
        return true;
    } catch (...) {
        output.clear();
        return false;
    }
}

bool decode_host_registration(std::span<const std::byte> input, HostRegistration& output) noexcept {
    if (input.size() != 76) {
        return false;
    }
    std::size_t offset{};
    HostRegistration decoded{};
    if (!read_bytes(input, offset, decoded.realmId.bytes)
        || !read_bytes(input, offset, decoded.hostId.bytes)
        || !read_bytes(input, offset, decoded.build.bytes)
        || !read_integer(input, offset, decoded.ipv4Address)
        || !read_integer(input, offset, decoded.udpBasePort)
        || !read_integer(input, offset, decoded.udpPortCount)
        || !read_integer(input, offset, decoded.activityCapacity)
        || !read_integer(input, offset, decoded.playerCapacity)) {
        return false;
    }
    output = decoded;
    return true;
}

bool encode_host_registration_result(const HostRegistrationResult& result,
                                     std::vector<std::byte>& output) noexcept {
    try {
        output.clear();
        output.reserve(26);
        append_bytes(output, result.realmId.bytes);
        append_integer(output, static_cast<std::uint16_t>(result.status));
        append_integer(output, result.heartbeatIntervalMilliseconds);
        append_integer(output, result.leaseTimeoutMilliseconds);
        return true;
    } catch (...) {
        output.clear();
        return false;
    }
}

bool decode_host_registration_result(std::span<const std::byte> input,
                                     HostRegistrationResult& output) noexcept {
    if (input.size() != 26) {
        return false;
    }
    std::size_t offset{};
    HostRegistrationResult decoded{};
    std::uint16_t status{};
    if (!read_bytes(input, offset, decoded.realmId.bytes) || !read_integer(input, offset, status)
        || !read_integer(input, offset, decoded.heartbeatIntervalMilliseconds)
        || !read_integer(input, offset, decoded.leaseTimeoutMilliseconds)
        || status > static_cast<std::uint16_t>(HostRegistrationStatus::capacityUnavailable)) {
        return false;
    }
    decoded.status = static_cast<HostRegistrationStatus>(status);
    output = decoded;
    return true;
}

bool encode_host_heartbeat(const HostHeartbeat& heartbeat,
                           std::vector<std::byte>& output) noexcept {
    try {
        output.clear();
        output.reserve(28);
        append_bytes(output, heartbeat.hostId.bytes);
        append_integer(output, heartbeat.monotonicMilliseconds);
        append_integer(output, heartbeat.activeActivities);
        append_integer(output, heartbeat.connectedPlayers);
        return true;
    } catch (...) {
        output.clear();
        return false;
    }
}

bool decode_host_heartbeat(std::span<const std::byte> input, HostHeartbeat& output) noexcept {
    if (input.size() != 28) {
        return false;
    }
    std::size_t offset{};
    HostHeartbeat decoded{};
    if (!read_bytes(input, offset, decoded.hostId.bytes)
        || !read_integer(input, offset, decoded.monotonicMilliseconds)
        || !read_integer(input, offset, decoded.activeActivities)
        || !read_integer(input, offset, decoded.connectedPlayers)) {
        return false;
    }
    output = decoded;
    return true;
}

bool encode_activity_spec(const ActivitySpec& activity, std::vector<std::byte>& output) noexcept {
    try {
        output.clear();
        output.reserve(40);
        append_bytes(output, activity.activityId.bytes);
        append_integer(output, activity.activityDefinitionHash);
        append_integer(output, activity.scenarioTagHash);
        append_integer(output, activity.modeDefinitionHash);
        append_integer(output, activity.maximumPlayers);
        append_integer(output, activity.scoreLimit);
        append_integer(output, activity.timeLimitSeconds);
        append_integer(output, activity.respawnDelayMilliseconds);
        return true;
    } catch (...) {
        output.clear();
        return false;
    }
}

bool decode_activity_spec(std::span<const std::byte> input, ActivitySpec& output) noexcept {
    if (input.size() != 40) {
        return false;
    }
    std::size_t offset{};
    ActivitySpec decoded{};
    if (!read_bytes(input, offset, decoded.activityId.bytes)
        || !read_integer(input, offset, decoded.activityDefinitionHash)
        || !read_integer(input, offset, decoded.scenarioTagHash)
        || !read_integer(input, offset, decoded.modeDefinitionHash)
        || !read_integer(input, offset, decoded.maximumPlayers)
        || !read_integer(input, offset, decoded.scoreLimit)
        || !read_integer(input, offset, decoded.timeLimitSeconds)
        || !read_integer(input, offset, decoded.respawnDelayMilliseconds)) {
        return false;
    }
    output = decoded;
    return true;
}

bool encode_activity_allocation_result(const ActivityAllocationResult& result,
                                       std::vector<std::byte>& output) noexcept {
    try {
        output.clear();
        output.reserve(18);
        append_bytes(output, result.activityId.bytes);
        append_integer(output, static_cast<std::uint16_t>(result.status));
        return true;
    } catch (...) {
        output.clear();
        return false;
    }
}

bool decode_activity_allocation_result(std::span<const std::byte> input,
                                       ActivityAllocationResult& output) noexcept {
    if (input.size() != 18) {
        return false;
    }
    std::size_t offset{};
    ActivityAllocationResult decoded{};
    std::uint16_t status{};
    if (!read_bytes(input, offset, decoded.activityId.bytes) || !read_integer(input, offset, status)
        || status > static_cast<std::uint16_t>(ActivityAllocationStatus::duplicate)) {
        return false;
    }
    decoded.status = static_cast<ActivityAllocationStatus>(status);
    output = decoded;
    return true;
}

} // namespace sunrise::realm::control
